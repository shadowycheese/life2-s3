#include "esp_log.h"
#include "gif/animated_gif.h"
#include "matrix/display.h"

class GIFPlayer
{
  public:
    void init(Display *display)
    {
        _display = display;
        _turbo_buffer = (uint8_t *)heap_caps_aligned_alloc(16, TURBO_BUFFER_SIZE + (64 * 64), MALLOC_CAP_SPIRAM);
        _buffer = (uint8_t *)heap_caps_aligned_alloc(16, 4 * (64 * 64), MALLOC_CAP_SPIRAM);
        ESP_LOGI(TAG, "Frame buf: %08X", _buffer);
    }

    int openData(const void *gif_data, int gif_size)
    {
        _gif.begin(GIF_PALETTE_RGB565_BE);

        if (_gif.open((uint8_t *)gif_data, gif_size, NULL))
        {
            ESP_LOGI(TAG, "Opened gif file (%dx%d)", _gif.getCanvasWidth(), _gif.getCanvasHeight());

            _gif.setTurboBuf(_turbo_buffer);
            _gif.setFrameBuf(_buffer);
            _gif.setDrawType(GIF_DRAW_COOKED);
        }
        else
        {
            ESP_LOGI(TAG, "Could not open gif");
            return GIF_FILE_NOT_OPEN;
        }

        return GIF_SUCCESS;
    }

    int play()
    {
        int delay;
        uint16_t *pixels;

        int cw = _gif.getCanvasWidth();
        int ch = _gif.getCanvasHeight();
        int w = _gif.getFrameWidth();
        int h = _gif.getFrameHeight();
        int xo = _gif.getFrameXOff();
        int yo = _gif.getFrameYOff();
        int rc = _gif.playFrame(&delay);

        if (!rc)
        {
            _gif.reset(); // loop forever
        }

        pixels = (uint16_t *)(_gif.getFrameBuf() + (cw * ch));

        pixels += xo + (yo * cw);

        for (int ty = 0; ty < h; ty++)
        {
            for (int tx = 0; tx < w; tx++)
            {
                uint16_t c1 = ((pixels[tx] >> 8) & 0xFF) | ((pixels[tx] & 0xFF) << 8);
                uint16_t c2 = RGB((c1 >> 12), (c1 >> 7), c1);

                int xx = xo + tx;
                int yy = yo + ty;

                _display->draw_pixel(xx, yy, c2);
            }
            pixels += cw;
        }
        return delay;
    }

    int close()
    {
        _gif.close();

        return 0;
    }

  protected:
    static constexpr const char *TAG = "GIF";

    uint8_t *_buffer;
    uint8_t *_turbo_buffer;
    AnimatedGIF _gif;
    Display *_display;
};