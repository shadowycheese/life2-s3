#include "gif/AnimatedGIF.h"
#include "matrix/display.h"
#include "esp_log.h"

class GIFPlayer
{
public:
    int openData(Display *display, const void *pData, int iDataSize)
    {
        int rc;
        void *pBuf;
        _gif.begin(GIF_PALETTE_RGB565_BE);
        if (_gif.open((uint8_t *)pData, iDataSize, NULL))
        {
            ESP_LOGI("GIF", "Opened gif file (%dx%d)", _gif.getCanvasWidth(), _gif.getCanvasHeight());
            // If the device has PSRAM, use it to enable TURBO mode
            _pTurboBuffer = (uint8_t *)heap_caps_aligned_alloc(16, TURBO_BUFFER_SIZE + (_gif.getCanvasWidth() * _gif.getCanvasHeight()), /*MALLOC_CAP_8BIT*/ MALLOC_CAP_SPIRAM);
            _gif.setTurboBuf(_pTurboBuffer);
            pBuf = heap_caps_aligned_alloc(16, _gif.getCanvasWidth() * _gif.getCanvasHeight() * 2, /*MALLOC_CAP_8BIT*/ MALLOC_CAP_SPIRAM);
            _gif.setFrameBuf(pBuf);
            _gif.setDrawType(GIF_DRAW_COOKED);
        }
        else
        {
            ESP_LOGI("GIF", "Could not open gif");
            return GIF_FILE_NOT_OPEN;
        }
        _display = display;
        return GIF_SUCCESS;
    }

    int play()
    {
        int delay;
        uint16_t *pPixels;

        int cw = _gif.getCanvasWidth();
        int ch = _gif.getCanvasHeight();
        int w = _gif.getFrameWidth();
        int h = _gif.getFrameHeight();
        int iX = _gif.getFrameXOff();
        int iY = _gif.getFrameYOff();
        int rc = _gif.playFrame(&delay);
        if (!rc)
        {
            _gif.reset(); // loop forever
        }

        ESP_LOGI("GIF", "canvas=%d,%d framw=%d,%d, offset=%d,%d", cw, ch, w, h, iX, iY);

        pPixels = (uint16_t *)(_gif.getFrameBuf() + (cw * ch));
        pPixels += iX + (iY * cw);

        for (int ty = 0; ty < h; ty++)
        {
            for (int tx = 0; tx < w; tx++)
            {
                uint16_t c1 = ((pPixels[tx] >> 8) & 0xFF) | ((pPixels[tx] & 0xFF) << 8);
                uint16_t c2 = RGB((c1 >> 12),
                                  (c1 >> 7),
                                  c1);

                int xx = iX + tx;
                int yy = iY + ty;

                if (yy > 32)
                {
                    continue;
                }

                _display->draw_pixel(xx, yy, c2);
            }
            pPixels += cw; // canvas width to the next line
        } // for y
        return delay;
    } /* play() */

    int close()
    {
        free(_gif.getFrameBuf());
        if (_pTurboBuffer)
        {
            free(_pTurboBuffer);
            _pTurboBuffer = nullptr;
        }
        _gif.close();
        return 0;
    }

protected:
    uint8_t *_pTurboBuffer;
    AnimatedGIF _gif;
    Display *_display;
};