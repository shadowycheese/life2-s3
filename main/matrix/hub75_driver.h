#pragma once

#include "hal/dma_types.h"
#include <driver/gpio.h>
#include <esp_heap_caps.h>
#include <esp_idf_version.h>
#include <esp_log.h>
#include <esp_private/gdma.h>
#include <esp_private/periph_ctrl.h>
#include <esp_rom_gpio.h>
#include <esp_rom_sys.h>
#include <hal/gdma_ll.h>
#include <hal/gpio_hal.h>
#include <soc/gpio_sig_map.h>
#include <soc/lcd_cam_struct.h>
#include <stddef.h>
#include <stdint.h>

#include <esp_private/gpio.h>

#define PIN_R1 GPIO_NUM_2
#define PIN_G1 GPIO_NUM_1
#define PIN_B1 GPIO_NUM_3
#define PIN_R2 GPIO_NUM_5
#define PIN_G2 GPIO_NUM_4
#define PIN_B2 GPIO_NUM_6

#define PIN_A GPIO_NUM_8
#define PIN_B GPIO_NUM_7
#define PIN_C GPIO_NUM_12
#define PIN_D GPIO_NUM_13

#define PIN_CLK GPIO_NUM_10
#define PIN_LAT GPIO_NUM_11
#define PIN_OE GPIO_NUM_9

#define PANEL_WIDTH 64
#define PANEL_HEIGHT 32
#define PANEL_COUNT 2

#define CLOCK_DIVIDER (32)

class Hub75Driver
{
  public:
    Hub75Driver() {};

    inline int width()
    {
        return _width;
    }

    inline int height()
    {
        return _height;
    }

    bool init()
    {
        ESP_LOGI(TAG, "Initializing LCD_CAM (I8080 16-bit) + GDMA for HUB75 64x32");

        periph_module_enable(PERIPH_LCD_CAM_MODULE);
        periph_module_reset(PERIPH_LCD_CAM_MODULE);

        LCD_CAM.lcd_user.lcd_reset = 1;

        esp_rom_delay_us(1000);

        configure_lcd_clock();
        configure_lcd_mode();
        configure_gpio();

        gdma_channel_alloc_config_t alloc_cfg = {};
        gdma_channel_handle_t chan = NULL;

        esp_err_t err = gdma_new_ahb_channel(&alloc_cfg, &chan, NULL);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "gdma_new_channel: %s", esp_err_to_name(err));
            return false;
        }
        _dma_chan = chan;

        ESP_ERROR_CHECK(gdma_connect(chan, GDMA_MAKE_TRIGGER(GDMA_TRIG_PERIPH_LCD, 0)));

        gdma_strategy_config_t strategy = {};
        gdma_apply_strategy(chan, &strategy);

        gdma_transfer_config_t transfer = {
            .max_data_burst_size = 32,
            .access_ext_mem = 0,
        };
        err = gdma_config_transfer(chan, &transfer);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "gdma transfer config: %s", esp_err_to_name(err));
            return false;
        }

        if (!allocate_dma())
        {
            return false;
        }

        build_descriptors();
        encode_frame(nullptr); // blank

        start();
        ESP_LOGI(TAG, "Running: %lu Hz clock", (unsigned long)(160000000 / CLOCK_DIVIDER));

        return true;
    }

    void set_frame_buffer(const uint16_t *fb_64x64)
    {
        encode_frame(fb_64x64);
    }

    void start()
    {
        LCD_CAM.lcd_user.lcd_update = 1;
        esp_rom_delay_us(10);
        gdma_start((gdma_channel_handle_t)_dma_chan, (intptr_t)_descriptors);
        esp_rom_delay_us(100);
        LCD_CAM.lcd_user.lcd_start = 1;
    }

  private:
    static constexpr int _width = PANEL_WIDTH;
    static constexpr int _height = PANEL_HEIGHT * PANEL_COUNT;
    static constexpr int _rows = 16;
    static constexpr int _row_words = 15 * (_width * PANEL_COUNT);
    static constexpr int _dma_words = _rows * (_row_words);
    static constexpr int _dma_bytes = _dma_words * sizeof(uint16_t);

    static constexpr int _oe_start = 0;
    static constexpr int _oe_end = _width - 1;

    enum : uint16_t
    {
        BitR1 = 0,
        BitG1 = 1,
        BitB1 = 2,
        BitR2 = 3,
        BitG2 = 4,
        BitB2 = 5,
        AddrShift = 6,
        BitLAT = 11,
        BitOE = 12,
    };

    inline uint16_t map_pixel(const uint16_t *fb, int x, int y);

    inline size_t round32(size_t size)
    {
        size_t mod = size % 32;

        if (mod == 0)
        {
            return size;
        }

        return size + (32 - mod);
    }

    void *_dma_chan = nullptr;
    void *_descriptors = nullptr;
    uint16_t *_dma_buf = nullptr;

    static constexpr const char *TAG = "HUB75";

    void configure_lcd_clock()
    {
        LCD_CAM.lcd_clock.lcd_clk_sel = 3;
        LCD_CAM.lcd_clock.lcd_ck_out_edge = 1;
        LCD_CAM.lcd_clock.lcd_ck_idle_edge = 0;
        LCD_CAM.lcd_clock.lcd_clkcnt_n = 1;
        LCD_CAM.lcd_clock.lcd_clk_equ_sysclk = 1;
        LCD_CAM.lcd_clock.lcd_clkm_div_num = CLOCK_DIVIDER;
        LCD_CAM.lcd_clock.lcd_clkm_div_a = 1;
        LCD_CAM.lcd_clock.lcd_clkm_div_b = 0;
    }

    void configure_lcd_mode()
    {
        LCD_CAM.lcd_ctrl.lcd_rgb_mode_en = 0;
        LCD_CAM.lcd_rgb_yuv.lcd_conv_bypass = 0;
        LCD_CAM.lcd_misc.lcd_next_frame_en = 0;
        LCD_CAM.lcd_misc.lcd_bk_en = 1;
        LCD_CAM.lcd_misc.lcd_vfk_cyclelen = 0;
        LCD_CAM.lcd_misc.lcd_vbk_cyclelen = 0;
        LCD_CAM.lcd_data_dout_mode.val = 0;

        LCD_CAM.lcd_user.lcd_always_out_en = 1;
        LCD_CAM.lcd_user.lcd_8bits_order = 0;
        LCD_CAM.lcd_user.lcd_bit_order = 0;
        LCD_CAM.lcd_user.lcd_2byte_en = 1;
        LCD_CAM.lcd_user.lcd_dout = 1;
        LCD_CAM.lcd_user.lcd_dummy = 1;
        LCD_CAM.lcd_user.lcd_dummy_cyclelen = 1;
        LCD_CAM.lcd_user.lcd_cmd = 0;
        LCD_CAM.lcd_user.lcd_start = 0;
        LCD_CAM.lcd_misc.lcd_afifo_reset = 1;
    }

    void configure_gpio()
    {
        static const struct
        {
            gpio_num_t pin;
            int idx;
        } data_map[] = {
            {PIN_R1, BitR1},
            {PIN_G1, BitG1},
            {PIN_B1, BitB1},
            {PIN_R2, BitR2},
            {PIN_G2, BitG2},
            {PIN_B2, BitB2},
            {PIN_A, AddrShift},
            {PIN_B, AddrShift + 1},
            {PIN_C, AddrShift + 2},
            {PIN_D, AddrShift + 3},
            {PIN_LAT, BitLAT},
            {PIN_OE, BitOE},
        };

        for (auto &m : data_map)
        {
            esp_rom_gpio_connect_out_signal(m.pin, LCD_DATA_OUT0_IDX + m.idx, false, false);
            gpio_func_sel(m.pin, PIN_FUNC_GPIO);
            gpio_set_drive_capability(m.pin, GPIO_DRIVE_CAP_3);
        }

        esp_rom_gpio_connect_out_signal(PIN_CLK, LCD_PCLK_IDX, false, false);
        gpio_func_sel(PIN_CLK, PIN_FUNC_GPIO);
        gpio_set_drive_capability(PIN_CLK, GPIO_DRIVE_CAP_3);
    }

    bool allocate_dma()
    {
        size_t dma_bytes = round32(_dma_bytes);

        _dma_buf = (uint16_t *)heap_caps_aligned_alloc(32, dma_bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
        if (!_dma_buf)
        {
            ESP_LOGE(TAG, "no DMA buffer memory (%d bytes)", _dma_bytes);
            return false;
        }

        size_t descriptor_bytes = round32(_rows * sizeof(dma_descriptor_t));

        _descriptors = heap_caps_aligned_alloc(32, descriptor_bytes, MALLOC_CAP_DMA);
        if (!_descriptors)
        {
            ESP_LOGE(TAG, "no descriptor memory");
            return false;
        }
        ESP_LOGI(TAG, "Allocated memory: %d DMA, %d descriptors", dma_bytes, descriptor_bytes);
        return true;
    }

    void build_descriptors()
    {
        dma_descriptor_t *desc = (dma_descriptor_t *)_descriptors;
        for (int g = 0; g < _rows; g++)
        {
            desc[g].dw0.size = _row_words * sizeof(uint16_t);
            desc[g].dw0.length = _row_words * sizeof(uint16_t);
            desc[g].dw0.owner = DMA_DESCRIPTOR_BUFFER_OWNER_DMA;
            desc[g].dw0.suc_eof = (g == _rows - 1);
            desc[g].buffer = (uint8_t *)&_dma_buf[g * _row_words];
            desc[g].next = (g == _rows - 1) ? &desc[0] : &desc[g + 1];
        }
    }

    void encode_frame(const uint16_t *fb)
    {
        const uint16_t panel_width = PANEL_WIDTH * PANEL_COUNT;

        int dma_pos = 0;

        for (int p = 0; p < 15; p++)
        {
            for (int g = 0; g < _rows; g++)
            {
                uint16_t visible_row_addr = ((uint16_t)((g - 1) & 0xF)) << AddrShift;
                uint16_t shifting_row_addr = ((uint16_t)(g) << AddrShift);

                for (int x = 0; x < panel_width; x++)
                {
                    uint16_t w = 0;

                    if (x == (panel_width - 2))
                    {
                        w |= shifting_row_addr | (1u << BitOE);
                    }
                    else if (x == (panel_width - 1))
                    {
                        w |= shifting_row_addr | (1u << BitOE) | (1u << BitLAT);
                    }
                    else
                    {
                        w |= visible_row_addr & ~(1u << BitOE);
                    }

                    if (fb)
                    {
                        int c2 = x % 64;

                        uint16_t r1, r2;

                        if (x >= 64)
                        {
                            r1 = fb[(size_t)(63 - g) * _width + (63 - c2)];
                            r2 = fb[(size_t)(63 - (g + _rows)) * _width + (63 - c2)];
                        }
                        else
                        {
                            r1 = fb[(size_t)g * _width + c2];
                            r2 = fb[(size_t)(g + _rows) * _width + c2];
                        }

                        w |= ((r1 >> 8) & 0xF) > p ? (1 << BitR1) : 0;
                        w |= ((r1 >> 4) & 0xF) > p ? (1 << BitG1) : 0;
                        w |= ((r1) & 0xF) > p ? (1 << BitB1) : 0;
                        w |= ((r2 >> 8) & 0xF) > p ? (1 << BitR2) : 0;
                        w |= ((r2 >> 4) & 0xF) > p ? (1 << BitG2) : 0;
                        w |= ((r2) & 0xF) > p ? (1 << BitB2) : 0;
                    }

                    _dma_buf[dma_pos++] = w;
                }
            }
        }
    }
};