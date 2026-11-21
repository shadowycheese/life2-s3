#include "gif/gif_player.h"
#include "esp_littlefs.h"

int32_t gif_read(GIFFILE *handle, uint8_t *buffer, int32_t length)
{
    int32_t iBytesRead;
    iBytesRead = length;
    FILE *f = (FILE *)handle->fHandle;

    if ((handle->iSize - handle->iPos) < length)
    {
        iBytesRead = handle->iSize - handle->iPos - 1;
    }
    if (iBytesRead <= 0)
    {
        return 0;
    }
    int br = (int32_t)fread(buffer, sizeof(uint8_t), iBytesRead, f);
    handle->iPos = ftell(f);
    return br;
}

int32_t gif_seek(GIFFILE *handle, int32_t iPosition)
{
    FILE *f = (FILE *)handle->fHandle;
    fseek(f, iPosition, SEEK_SET);
    handle->iPos = (int32_t)ftell(f);
    return handle->iPos;
}
