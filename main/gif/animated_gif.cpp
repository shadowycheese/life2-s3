#include "gif/animated_gif.h"
#include "gif/gif.inl"
#include "esp_log.h"

int AnimatedGIF::open(uint8_t *pData, int iDataSize, GIF_DRAW_CALLBACK *pfnDraw)
{
    _gif.iError = GIF_SUCCESS;
    _gif.pfnRead = readMem;
    _gif.pfnSeek = seekMem;
    _gif.pfnDraw = pfnDraw;
    _gif.GIFFile.iSize = iDataSize;
    _gif.GIFFile.pData = pData;
    return GIFInit(&_gif);
}

void AnimatedGIF::mergeTransparent(uint8_t *pSrc, uint8_t *pDst, uint8_t ucTrans, int iLen)
{
    GIF_mergeTransparent(pSrc, pDst, ucTrans, iLen);
}

void AnimatedGIF::cookPixels(uint8_t *pSrc, uint8_t *pDst, int iTrans, int iLen, uint32_t *pPalette, uint16_t *pRGB565)
{
    GIF_cookPixels(pSrc, pDst, iTrans, iLen, pPalette, pRGB565);
}

int AnimatedGIF::getComment(char *pDest)
{
    int32_t iOldPos;

    iOldPos = _gif.GIFFile.iPos;
    (*_gif.pfnSeek)(&_gif.GIFFile, _gif.iCommentPos);
    (*_gif.pfnRead)(&_gif.GIFFile, (uint8_t *)pDest, _gif.sCommentLen);
    (*_gif.pfnSeek)(&_gif.GIFFile, iOldPos);
    pDest[_gif.sCommentLen] = 0;
    return (int)_gif.sCommentLen;
}

void AnimatedGIF::setFrameBuf(void *pFrameBuf)
{
    _gif.pFrameBuffer = (uint8_t *)pFrameBuf;
}
void AnimatedGIF::setTurboBuf(void *pBuf)
{
    _gif.pTurboBuffer = (uint8_t *)pBuf;
}
int AnimatedGIF::setDrawType(int iType)
{
    if (iType != GIF_DRAW_RAW && iType != GIF_DRAW_COOKED)
    {
        return GIF_INVALID_PARAMETER; // invalid drawing mode
    }
    _gif.ucDrawType = (uint8_t)iType;
    return GIF_SUCCESS;
}

uint8_t *AnimatedGIF::getFrameBuf()
{
    return _gif.pFrameBuffer;
}
uint8_t *AnimatedGIF::getTurboBuf()
{
    return _gif.pTurboBuffer;
}

int AnimatedGIF::getCanvasWidth()
{
    return _gif.iCanvasWidth;
}

int AnimatedGIF::getFrameWidth()
{
    return _gif.iWidth;
}
int AnimatedGIF::getFrameHeight()
{
    return _gif.iHeight;
}

int AnimatedGIF::getFrameXOff()
{
    return _gif.iX;
}

int AnimatedGIF::getFrameYOff()
{
    return _gif.iY;
}

int AnimatedGIF::getCanvasHeight()
{
    return _gif.iCanvasHeight;
}
int AnimatedGIF::getLoopCount()
{
    return _gif.iRepeatCount;
}

int AnimatedGIF::getInfo(GIFINFO *pInfo)
{
    return GIF_getInfo(&_gif, pInfo);
}

int AnimatedGIF::getLastError()
{
    return _gif.iError;
}

void AnimatedGIF::reset()
{
    _gif.iError = GIF_SUCCESS;
    (*_gif.pfnSeek)(&_gif.GIFFile, 0);
}

void AnimatedGIF::begin(unsigned char ucPaletteType)
{
    uint8_t *p;
    uint32_t u32;

    memset(&_gif, 0, sizeof(_gif));
    if (ucPaletteType != GIF_PALETTE_RGB565_LE && ucPaletteType != GIF_PALETTE_RGB565_BE && ucPaletteType != GIF_PALETTE_RGB888)
    {
        _gif.iError = GIF_INVALID_PARAMETER;
    }
    _gif.ucPaletteType = ucPaletteType;
    _gif.ucDrawType = GIF_DRAW_RAW; // assume RAW pixel handling
    _gif.pFrameBuffer = NULL;
    p = &_gif.ucLineBuf[0];
    u32 = (uint32_t)(intptr_t)p;
    u32 &= 15; // align on 16-byte boundary
    if (u32 > 0)
    {
        p += (16 - u32);
    }
    _gif.pLineBufAligned = p;
}

int AnimatedGIF::playFrame(int *delayMilliseconds)
{
    int rc;

    if (_gif.GIFFile.iPos >= _gif.GIFFile.iSize - 1) // no more data exists
    {
        (*_gif.pfnSeek)(&_gif.GIFFile, 0); // seek to start
    }
    if (GIFParseInfo(&_gif, 0))
    {
        if (_gif.iError == GIF_EMPTY_FRAME) // don't try to decode it
        {
            return 0;
        }
        if (_gif.pTurboBuffer)
        {
            rc = DecodeLZWTurbo(&_gif, 0);
        }
        else
        {
            rc = DecodeLZW(&_gif, 0);
        }
        if (rc != 0) // problem
        {
            return -1;
        }
    }
    else
    {
        // The file is "malformed" in that there is a bunch of non-image data after
        // the last frame. Return as if all is well, though if needed getLastError()
        // can be used to see if a frame was actually processed:
        // GIF_SUCCESS -> frame processed, GIF_EMPTY_FRAME -> no frame processed
        if (_gif.iError == GIF_EMPTY_FRAME)
        {
            if (delayMilliseconds)
            {
                *delayMilliseconds = 0;
            }
            return 0;
        }
        return -1; // error parsing the frame info, we may be at the end of the file
    }
    // Return 1 for more frames or 0 if this was the last frame
    if (delayMilliseconds) // if not NULL, return the frame delay time
    {
        *delayMilliseconds = _gif.iFrameDelay;
    }
    return (_gif.GIFFile.iPos < _gif.GIFFile.iSize - 10);
}