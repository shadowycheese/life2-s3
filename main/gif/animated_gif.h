#ifndef ANIMATEDGIF_H
#define ANIMATEDGIF_H

#include <stdint.h>
#include <string.h>

#define memcpy_P memcpy
#define PROGMEM

/* GIF Defines and variables */
#define MAX_CHUNK_SIZE 255

#define TURBO_BUFFER_SIZE 0x6100

#define MAX_CODE_SIZE 12

#define MAX_COLORS 256
#define MAX_WIDTH 480
#define LZW_BUF_SIZE (6 * MAX_CHUNK_SIZE)
#define LZW_HIGHWATER (4 * MAX_CHUNK_SIZE)
#define FILE_BUF_SIZE (1 << MAX_CODE_SIZE)

#define PIXEL_FIRST 0
#define PIXEL_LAST (1 << MAX_CODE_SIZE)
#define LINK_UNUSED 5911 // 0x1717 to use memset
#define LINK_END 5912
#define MAX_HASH 5003
#define LZW_BUF_SIZE_TURBO (LZW_BUF_SIZE + (2 << MAX_CODE_SIZE) + (PIXEL_LAST * 2) + MAX_WIDTH)
#define LZW_HIGHWATER_TURBO ((LZW_BUF_SIZE_TURBO * 14) / 16)

enum
{
  GIF_PALETTE_RGB565_LE = 0, // little endian (default)
  GIF_PALETTE_RGB565_BE,     // big endian
  GIF_PALETTE_RGB888,        // original 24-bpp entries
  GIF_PALETTE_RGB8888,       // 32-bit (alpha = 0xff)
  GIF_PALETTE_1BPP,          // 1-bit per pixel (horizontal, MSB on left)
  GIF_PALETTE_1BPP_OLED      // 1-bit per pixel (vertical, LSB on top)
};

enum
{
  GIF_DRAW_RAW = 0,
  GIF_DRAW_COOKED
};

enum
{
  GIF_SUCCESS = 0,
  GIF_DECODE_ERROR,
  GIF_TOO_WIDE,
  GIF_INVALID_PARAMETER,
  GIF_UNSUPPORTED_FEATURE,
  GIF_FILE_NOT_OPEN,
  GIF_EARLY_EOF,
  GIF_EMPTY_FRAME,
  GIF_BAD_FILE,
  GIF_ERROR_MEMORY
};

typedef struct gif_file_tag
{
  int32_t iPos;   // current file position
  int32_t iSize;  // file size
  uint8_t *pData; // memory file pointer
  void *fHandle;  // class pointer to File/SdFat or whatever you want
} GIFFILE;

typedef struct gif_info_tag
{
  int32_t iFrameCount; // total frames in file
  int32_t iDuration;   // duration of animation in milliseconds
  int32_t iMaxDelay;   // maximum frame delay
  int32_t iMinDelay;   // minimum frame delay
} GIFINFO;

typedef struct gif_draw_tag
{
  int iX, iY;                // Corner offset of this frame on the canvas
  int y;                     // current line being drawn (0 = top line of image)
  int iWidth, iHeight;       // size of this frame
  int iCanvasWidth;          // need this to know where to place output in a fully cooked bitmap
  void *pUser;               // user supplied pointer
  uint8_t *pPixels;          // 8-bit source pixels for this line
  uint16_t *pPalette;        // little or big-endian RGB565 palette entries (default)
  uint8_t *pPalette24;       // RGB888 palette (optional)
  uint8_t ucTransparent;     // transparent color
  uint8_t ucHasTransparency; // flag indicating the transparent color is in use
  uint8_t ucDisposalMethod;  // frame disposal method
  uint8_t ucBackground;      // background color
  uint8_t ucPaletteType;     // type of palette entries
  uint8_t ucIsGlobalPalette; // Flag to indicate that a global palette, rather than a local palette is being used
} GIFDRAW;

// Callback function prototypes
typedef int32_t(GIF_READ_CALLBACK)(GIFFILE *pFile, uint8_t *pBuf, int32_t iLen);
typedef int32_t(GIF_SEEK_CALLBACK)(GIFFILE *pFile, int32_t iPosition);
typedef void(GIF_DRAW_CALLBACK)(GIFDRAW *pDraw);
typedef void *(GIF_OPEN_CALLBACK)(const char *szFilename, int32_t *pFileSize);
typedef void(GIF_CLOSE_CALLBACK)(void *pHandle);
typedef void *(GIF_ALLOC_CALLBACK)(uint32_t iSize);
typedef void(GIF_FREE_CALLBACK)(void *buffer);
//
// our private structure to hold a GIF image decode state
//
typedef struct gif_image_tag
{
  uint16_t iWidth, iHeight, iCanvasWidth, iCanvasHeight;
  uint16_t iPrevW, iPrevH, iPrevX, iPrevY; // for disposal method 2 (restore to bg)
  uint16_t iX, iY;                         // GIF corner offset
  uint16_t iBpp;
  int16_t iError;            // last error
  uint16_t iFrameDelay;      // delay in milliseconds for this frame
  int16_t iRepeatCount;      // NETSCAPE animation repeat count. 0=forever
  uint16_t iXCount, iYCount; // decoding position in image (countdown values)
  int iLZWOff;               // current LZW data offset
  int iLZWSize;              // current quantity of data in the LZW buffer
  int iCommentPos;           // file offset of start of comment data
  short sCommentLen;         // length of comment
  unsigned char bEndOfFrame;
  unsigned char ucPrevDisp, ucDisposalMethod;
  unsigned char ucGIFBits, ucBackground, ucTransparent, ucCodeStart, ucMap, bUseLocalPalette;
  unsigned char ucPaletteType; // RGB565 or RGB888
  unsigned char ucDrawType;    // RAW or COOKED
  GIF_READ_CALLBACK *pfnRead;
  GIF_SEEK_CALLBACK *pfnSeek;
  GIF_DRAW_CALLBACK *pfnDraw;
  GIFFILE GIFFile;
  void *pUser;
  unsigned char *pFrameBuffer;
  unsigned char *pTurboBuffer;
  unsigned char *pPixels, *pOldPixels;
  unsigned char ucFileBuf[FILE_BUF_SIZE];             // holds temp data and pixel stack
  unsigned short pPalette[(MAX_COLORS * 3) / 2];      // can hold RGB565 or RGB888 - set in begin()
  unsigned short pLocalPalette[(MAX_COLORS * 3) / 2]; // color palettes for GIF images
  int iLocalPalSize, iGlobalPalSize;
  uint8_t ucLZW[LZW_BUF_SIZE]; // holds de-chunked LZW data
  // These next 3 are used in Turbo mode to have a larger ucLZW buffer
  uint16_t usGIFTable[1 << MAX_CODE_SIZE];
  uint8_t ucGIFPixels[(PIXEL_LAST * 2)];
  uint8_t ucLineBuf[MAX_WIDTH + 15]; // current line
  uint8_t *pLineBufAligned;
} GIFIMAGE;

#ifdef __cplusplus
//
// The GIF class wraps portable C code which does the actual work
//
class AnimatedGIF
{
public:
  int open(uint8_t *pData, int iDataSize, GIF_DRAW_CALLBACK *pfnDraw);
  void close();
  void reset();
  void begin(uint8_t ucPaletteType = GIF_PALETTE_RGB565_LE);
  void begin(int iEndian, uint8_t ucPaletteType) { begin(ucPaletteType); };
  int playFrame(int *);
  int getCanvasWidth();
  int getFrameWidth();
  int getFrameHeight();
  int getFrameXOff();
  int getFrameYOff();
  void setTurboBuf(void *pTurboBuffer);
  void setFrameBuf(void *pFrameBuffer);
  int setDrawType(int iType);
  uint8_t *getFrameBuf();
  uint8_t *getTurboBuf();
  int getCanvasHeight();
  int getLoopCount();
  int getInfo(GIFINFO *pInfo);
  int getLastError();
  int getComment(char *destBuffer);
  void mergeTransparent(uint8_t *pSrc, uint8_t *pDst, uint8_t ucTrans, int iLen);
  void cookPixels(uint8_t *pSrc, uint8_t *pDst, int iTrans, int iLen, uint32_t *pPalette, uint16_t *pRGB565);

private:
  GIFIMAGE _gif;
};

// Due to unaligned memory causing an exception, we have to do these macros the slow way
#define REGISTER_WIDTH 32
#define INTELSHORT(p) ((*p) + (*(p + 1) << 8))
#define INTELLONG(p) ((*p) + (*(p + 1) << 8) + (*(p + 2) << 16) + (*(p + 3) << 24))
#define BIGINT int32_t
#define BIGUINT uint32_t

#endif
#endif