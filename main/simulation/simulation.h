#ifndef SIM4_H
#define SIM4_H

/*
Pattern        | f       | k
---------------|---------|---------
Mitosis        | 0.0367  | 0.0649
Coral Growth   | 0.0545  | 0.062
Fingerprint    | 0.055   | 0.062
Spirals        | 0.018   | 0.051
Worms          | 0.078   | 0.061
Maze           | 0.029   | 0.057
Bubbles        | 0.098   | 0.057
Spots and Loops| 0.039   | 0.058
Waves          | 0.014   | 0.054
Splotches      | 0.026   | 0.051
Solitions      | 0.03    | 0.06
Dots           | 0.05    | 0.065
Stripes        | 0.025   | 0.06
Cross Waves    | 0.012   | 0.045
Mixed          | 0.022   | 0.051
*/

#include "esp_log.h"
#include "simulation/sim_model.h"
#include <esp_heap_caps.h>
#include <esp_random.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

typedef struct
{
    char name[20];
    float feed_rate;
    float kill_rate;
} sim_preset_t;

typedef struct
{
    float A;
    float B;
} cell_t;

typedef struct
{
    uint8_t r, g, b;
} rgb_t;

#define ap(x, y) (((y) * _width) + (x))

class Simulation
{
  public:
    void init(int width, int height)
    {
        _width = width;
        _height = height;
        _hue = 0;

        int array_size = _width * _height;

        size_t grid_bytes = sizeof(cell_t) * array_size;

        _grid_1 = (cell_t *)(void *)heap_caps_aligned_alloc(4, grid_bytes, MALLOC_CAP_INTERNAL);
        _grid_2 = (cell_t *)(void *)heap_caps_aligned_alloc(4, grid_bytes, MALLOC_CAP_INTERNAL);

        ESP_LOGI("SIM", "Allocated: %dx2: g1=%08X, g2=%08X", grid_bytes, _grid_1, _grid_2);

        _grid = _grid_1;
        _temp_grid = _grid_2;

        reset();

        _spawn_chance = 1250;
        _feed_rate = _presets[8].feed_rate;
        _kill_rate = _presets[8].kill_rate;

        srand(esp_random());
    }

    void set_configuration(sim_configuration_t *config)
    {
        if (config->magic == SIM_CONFIG_MAGIC)
        {
            //_feed_rate = config->feed_rate;
            //_kill_rate = config->kill_rate;
            //_hue = config->hue % 6;
            //_spawn_chance = config->spawn_chance;
        }
    }

    void get_configuration(sim_configuration_t *config)
    {
        config->feed_rate = _feed_rate;
        config->kill_rate = _kill_rate;
        config->spawn_chance = _spawn_chance;
        config->hue = _hue;
        config->magic = SIM_CONFIG_MAGIC;
    }

    inline int spawn_chance()
    {
        return _spawn_chance;
    }

    inline int set_spawn_chance(int spawn_chance)
    {
        _spawn_chance = spawn_chance;

        return _spawn_chance;
    }

    inline float set_feed_rate(float feed_rate)
    {
        _feed_rate = constrain(feed_rate, 0.0f, 1.0f);

        return _feed_rate;
    }

    inline float set_kill_rate(float kill_rate)
    {
        _kill_rate = constrain(kill_rate, 0.0f, 1.0f);

        return _kill_rate;
    }

    inline float kill_rate()
    {
        return _kill_rate;
    }

    inline float feed_rate()
    {
        return _feed_rate;
    }

    inline void next_hue()
    {
        _hue = (_hue + 1) % 6;
    }

    inline void add(int16_t x, int16_t y)
    {
        _grid[ap(x, y)].B = 1.0f;
    }

    void reset()
    {
        int array_size = _width * _height;

        for (int i = 0; i < array_size; i++)
        {
            _grid[i] = {.A = 1.0, .B = 0.0};
            _temp_grid[i] = {.A = 1.0, .B = 0.0};
        }
    }

    const char *name()
    {
        for (int i = 0; i < 15; i++)
        {
            if ((_feed_rate == _presets[i].feed_rate) && (_kill_rate == _presets[i].kill_rate))
            {
                return _presets[i].name;
            }
        }

        return "";
    }

    int16_t preset()
    {
        for (int i = 0; i < 15; i++)
        {
            if ((_feed_rate == _presets[i].feed_rate) && (_kill_rate == _presets[i].kill_rate))
            {
                return i;
            }
        }

        return -1;
    }

    void set_preset(int preset)
    {
        preset = (preset) % 15;

        _feed_rate = _presets[preset].feed_rate;
        _kill_rate = _presets[preset].kill_rate;
    }

    void simulate()
    {
        if ((random() % 10000) < _spawn_chance)
        {
            spawn();
        }

        step_simulation();
    }

    uint16_t get_colour(int x, int y)
    {
        int p = ap(x, y);

        float l = constrain(1.0f - (_grid[p].A - _grid[p].B), 0.0f, 1.0f);

        return map_grayscott_rgb444(l) & 0xFFF;
    }

  private:
    const sim_preset_t _presets[15] = {
        {"Mitosos", 0.0367, 0.0649}, // Mitosis
        {"Coral", 0.0545, 0.062},    // Coral Growth
        {"Fingerpr.", 0.055, 0.062}, // Fingerprint
        {"Spiral", 0.018, 0.051},    // Spirals
        {"Worm", 0.078, 0.061},      // Worms
        {"Maze", 0.029, 0.057},      // Maze
        {"Bubbles", 0.098, 0.057},   // Bubbles
        {"Spot/Loop", 0.039, 0.058}, // Spots and Loops
        {"Waves", 0.014, 0.054},     // Waves
        {"Splotch", 0.026, 0.051},   // Splotches
        {"Solition", 0.03, 0.06},    // Solitions
        {"Dot", 0.05, 0.065},        // Dots
        {"Stripe", 0.025, 0.06},     // Stripes
        {"X/Wave", 0.012, 0.045},    // Cross Waves
        {"Mixed", 0.022, 0.051},     // Mixed
    };

    int _width;
    int _height;

    int _hue;

    cell_t *_grid_1;
    cell_t *_grid_2;

    cell_t *_grid;
    cell_t *_temp_grid;

    float _D_A = 1.0;
    float _D_B = 0.5;
    float _feed_rate;
    float _kill_rate;
    int16_t _spawn_chance;

    __attribute__((always_inline)) inline uint16_t pack_band_colour(float f, float r0, float g0, float b0,
                                                                    float r1, float g1, float b1)
    {
        const uint16_t r = (uint16_t)((r0 + (r1 - r0) * f) + 0.5f);
        const uint16_t g = (uint16_t)((g0 + (g1 - g0) * f) + 0.5f);
        const uint16_t b = (uint16_t)((b0 + (b1 - b0) * f) + 0.5f);

        switch (_hue)
        {
        case 0:
            return RGB(r, g, b);
        case 1:
            return RGB(r, b, g);
        case 2:
            return RGB(g, r, b);
        case 3:
            return RGB(g, b, r);
        case 4:
            return RGB(b, r, g);
        case 5:
            return RGB(b, g, r);
        default:
            return RGB(r, g, b);
        }
    }

    uint16_t map_grayscott_rgb444(float val)
    {
        // Out-of-range and NaN values produced black in the original loop as well.
        if (!(val <= 1.0f))
        {
            return 0;
        }

        // Four fixed bands: no loop, no runtime band lookup, and the inverse
        // widths are folded into constants at compile time.
        if (val <= 0.40f)
        {
            return pack_band_colour(val * (1.0f / (0.40f - 0.00f)),
                                    0.0f, 0.0f, 0.0f, 4.0f, 1.0f, 9.0f);
        }
        if (val <= 0.60f)
        {
            return pack_band_colour((val - 0.40f) * (1.0f / (0.60f - 0.40f)),
                                    4.0f, 1.0f, 9.0f, 9.0f, 0.0f, 13.0f);
        }
        if (val <= 0.80f)
        {
            return pack_band_colour((val - 0.60f) * (1.0f / (0.80f - 0.60f)),
                                    9.0f, 0.0f, 13.0f, 11.0f, 3.0f, 2.0f);
        }

        return pack_band_colour((val - 0.80f) * (1.0f / (1.00f - 0.80f)),
                                11.0f, 3.0f, 2.0f, 9.0f, 8.0f, 1.0f);
    }

    inline void spawn()
    {
        _grid[random() % (_width * _height)].B = 1.0f;
    }

    inline float constrain(float value, float low, float high)
    {
        return value > high ? high : (value < low ? low : value);
    }

    inline float clamp01(float value)
    {
        return value > 1.0f ? 1.0f : (value < 0.0f ? 0.0f : value);
    }

    void step_simulation()
    {
        cell_t *__restrict src = _grid;
        cell_t *__restrict dst = _temp_grid;

        const float d_a = _D_A;
        const float d_b = _D_B;
        const float feed = _feed_rate;
        const float react_kill = _kill_rate + _feed_rate;

        const int width = _width;
        const int height = _height;

        // Row-major traversal: consecutive cells (and the three active rows) stay in cache.
        for (int y = 0; y < height; y++)
        {
            const int row = y * width;
            const int up_row = (y == 0 ? height - 1 : y - 1) * width;
            const int down_row = (y == height - 1 ? 0 : y + 1) * width;

            for (int x = 0; x < width; x++)
            {
                const int left = x == 0 ? width - 1 : x - 1;
                const int right = x == width - 1 ? 0 : x + 1;

                // Both chemicals are computed from the same nine neighbour reads.
                const cell_t &c = src[row + x];
                const cell_t &cell_right = src[row + right];
                const cell_t &cell_left = src[row + left];
                const cell_t &cell_down = src[down_row + x];
                const cell_t &cell_up = src[up_row + x];
                const cell_t &cell_left_up = src[up_row + left];
                const cell_t &cell_right_up = src[up_row + right];
                const cell_t &cell_left_down = src[down_row + left];
                const cell_t &cell_right_down = src[down_row + right];

                const float lap_a =
                    0.2f * (cell_right.A + cell_left.A + cell_down.A + cell_up.A) +
                    0.05f * (cell_left_up.A + cell_right_up.A + cell_left_down.A + cell_right_down.A) -
                    c.A;
                const float lap_b =
                    0.2f * (cell_right.B + cell_left.B + cell_down.B + cell_up.B) +
                    0.05f * (cell_left_up.B + cell_right_up.B + cell_left_down.B + cell_right_down.B) -
                    c.B;

                const float abb = c.A * c.B * c.B;

                cell_t &out = dst[row + x];
                out.A = clamp01(c.A + (d_a * lap_a - abb + feed * (1.0f - c.A)));
                out.B = clamp01(c.B + (d_b * lap_b + abb - react_kill * c.B));
            }
        }

        if (_grid == _grid_1)
        {
            _grid = _grid_2;
            _temp_grid = _grid_1;
        }
        else
        {
            _grid = _grid_1;
            _temp_grid = _grid_2;
        }
    }
};
#endif
