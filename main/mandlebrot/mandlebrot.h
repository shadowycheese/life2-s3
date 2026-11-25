#ifndef MANDLEBROT_H
#define MANDLEBROT_H

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>

#define MAX_LOOPS (64)
#define SUB_FRAMES (128)

// A small set of keyframes describing one closed tour. The frames between
// them are generated at runtime (see preparePath/step), so the expensive part
// of each step is still rendering the Mandelbrot, not walking the path.
#define ZOOM_KEYS 24

// Iteration counts are turned into a smooth (fractional) value and run
// through a repeating palette. One cycle every COLOUR_CYCLE iterations means
// the image gains colour bands as you zoom in, instead of getting darker.
#define PALETTE_SIZE 256
#define COLOUR_CYCLE 64.0f
#define PALETTE_MAX_CHANNEL 0x8 // palette is scaled into 0..PALETTE_MAX_CHANNEL

typedef struct
{
    float real;
    float imag;
    float scale;
    uint16_t max_iter;
    uint16_t frames; // interpolated frames from this key to the next one
} zoom_key_t;

class Mandlebrot
{
  public:
    Mandlebrot()
    {
    }

    void init(int w, int h)
    {
        _w = w;
        _h = h;

        _x = 0.2f;
        _y = -0.4f;
        _size = 0.5f;
        _frame = 0;

        int array_size = _w * _h;

        size_t grid_bytes = sizeof(uint16_t) * array_size;

        _grid = (uint16_t *)(void *)heap_caps_aligned_alloc(4, grid_bytes, MALLOC_CAP_INTERNAL);

        buildPalette();
        preparePath();
    }

    // The grid holds ready-to-display 12-bit RGB colours, not iteration counts.
    uint16_t colour(int x, int y)
    {
        return _grid[(y * _w) + x];
    }

    // Returns the 12-bit colour for one point (0 means "inside the set").
    __attribute__((always_inline)) inline uint16_t calc(float r, float i, int max_loops)
    {
        // The main cardioid and the period-2 bulb are wholly inside the set;
        // skipping their iterations avoids most of the work for interior pixels.
        const float x0 = r - 0.25f;
        const float x02 = x0 * x0;
        const float i2 = i * i;
        const float q = x02 + i2;

        if ((q * (q + x0)) <= (0.25f * i2))
        {
            return 0;
        }

        const float xp = r + 1.0f;
        if ((xp * xp + i2) <= 0.0625f)
        {
            return 0;
        }

        float zr = 0.0f;
        float zi = 0.0f;
        float zr2 = 0.0f;
        float zi2 = 0.0f;

        // Keep zr^2 and zi^2 between iterations: the escape test and the next
        // z = z^2 + c both need them, so each is computed once instead of twice.
        int loop = 0;

        while (((zr2 + zi2) <= 4.0f) && (loop < max_loops))
        {
            float ni = (2.0f * zr * zi) + i;

            zr = zr2 - zi2 + r;
            zi = ni;

            zr2 = zr * zr;
            zi2 = zi * zi;

            loop++;
        }

        if (loop == max_loops)
        {
            return 0;
        }

        // Fractional escape count: loop minus the fractional part of the last
        // iteration. This removes the hard bands caused by a whole-number count.
        // With escape at |z| = 2 the correction is log2(log2|z|).
        const float smooth = (float)loop - log2f(0.5f * log2f(zr2 + zi2));

        // Advance through the repeating palette once per COLOUR_CYCLE iterations.
        int index = (int)(smooth * ((float)PALETTE_SIZE / COLOUR_CYCLE)) & (PALETTE_SIZE - 1);

        return _palette[index];
    }

    void step()
    {
        // Find the keyframe segment containing the current frame.
        int s = _frame;
        int k = 0;
        while ((k < ZOOM_KEYS - 1) && (s >= _durations[k]))
        {
            s -= _durations[k];
            k++;
        }

        const int h = _durations[k];
        const double t = (double)s / (double)h;
        const double t2 = t * t;
        const double t3 = t2 * t;
        const int kn = (k + 1) % ZOOM_KEYS;

        // Cubic Hermite basis functions. Tangents are shape-preserving and
        // scaled by the segment duration, so velocity stays continuous at
        // keyframes without overshooting into solid interior regions.
        const double h00 = 2.0 * t3 - 3.0 * t2 + 1.0;
        const double h10 = t3 - 2.0 * t2 + t;
        const double h01 = -2.0 * t3 + 3.0 * t2;
        const double h11 = t3 - t2;

        const double real = h00 * _values[k][0] + h10 * h * _tangents[k][0] + h01 * _values[kn][0] + h11 * h * _tangents[kn][0];
        const double imag = h00 * _values[k][1] + h10 * h * _tangents[k][1] + h01 * _values[kn][1] + h11 * h * _tangents[kn][1];
        const double logScale =
            h00 * _values[k][2] + h10 * h * _tangents[k][2] + h01 * _values[kn][2] + h11 * h * _tangents[kn][2];
        const double iterF = h00 * _values[k][3] + h10 * h * _tangents[k][3] + h01 * _values[kn][3] + h11 * h * _tangents[kn][3];

        // Zoom geometrically: constant octaves per frame, no scale overshoot.
        const float scale = (float)pow(2.0, logScale);
        const int max_iter = (int)(iterF + 0.5);

        const int width = _w;
        const int height = _h;
        const float step_x = scale / (float)width;
        const float step_y = scale / (float)height;
        const float real_start = (float)real - scale / 2;

        uint16_t *__restrict out = _grid;
        float i = (float)imag - scale / 2;

        for (int y = 0; y < height; y++)
        {
            float r = real_start;

            for (int x = 0; x < width; x++)
            {
                *out++ = calc(r, i, max_iter);
                r += step_x;
            }

            i += step_y;
        }

        if (++_frame == _total_frames)
        {
            _frame = 0;
        }
    }

  private:
    int _w, _h;

    int _frame;
    int _total_frames;

    float _x;
    float _y;
    float _size;
    uint16_t *_grid;

    // Path fields are [real, imag, log2(scale), max_iter].
    double _values[ZOOM_KEYS][4];
    double _tangents[ZOOM_KEYS][4];
    int _durations[ZOOM_KEYS];

    uint16_t _palette[PALETTE_SIZE];

    void buildPalette()
    {
        // One continuous loop of colours, dark at both ends so it can repeat
        // without a visible seam: violet -> blue -> cyan -> white -> gold ->
        // red -> violet. Only the interior stays truly black.
        struct stop_t
        {
            int pos;
            int r;
            int g;
            int b;
        };

        static const stop_t stops[] = {
            {0, 1, 0, 3},
            {22, 3, 0, 10},
            {46, 1, 4, 15},
            {70, 0, 10, 15},
            {86, 0, 15, 15},
            {98, 8, 15, 15},
            {106, 15, 15, 15},
            {118, 15, 14, 2},
            {132, 15, 9, 0},
            {150, 15, 3, 0},
            {172, 9, 0, 4},
            {200, 4, 0, 8},
            {230, 2, 0, 6},
            {255, 1, 0, 3},
        };

        const int count = sizeof(stops) / sizeof(stops[0]);

        for (int i = 0; i < PALETTE_SIZE; i++)
        {
            int s = 0;
            while ((s < count - 2) && (i >= stops[s + 1].pos))
            {
                s++;
            }

            const stop_t &a = stops[s];
            const stop_t &b = stops[s + 1];
            const int span = b.pos - a.pos;
            const int num = i - a.pos;

            const int r15 = a.r + ((b.r - a.r) * num) / span;
            const int g15 = a.g + ((b.g - a.g) * num) / span;
            const int b15 = a.b + ((b.b - a.b) * num) / span;

            // Scale the whole ramp down rather than clipping the highlights,
            // so bright stops keep their gradient while capping every channel.
            const int r = (r15 * PALETTE_MAX_CHANNEL + 7) / 15;
            const int g = (g15 * PALETTE_MAX_CHANNEL + 7) / 15;
            const int bl = (b15 * PALETTE_MAX_CHANNEL + 7) / 15;

            _palette[i] = (uint16_t)((r << 8) | (g << 4) | bl);
        }
    }

    void preparePath()
    {
        _total_frames = 0;

        for (int i = 0; i < ZOOM_KEYS; i++)
        {
            _durations[i] = _keys[i].frames;
            _total_frames += _keys[i].frames;
            _values[i][0] = _keys[i].real;
            _values[i][1] = _keys[i].imag;
            _values[i][2] = log2((double)_keys[i].scale);
            _values[i][3] = _keys[i].max_iter;
        }

        // Fritsch-Carlson tangents for a closed, non-uniformly sampled path.
        // Setting a tangent to zero at a local extremum keeps the curve
        // monotone between keys, which prevents overshoot into the set.
        for (int f = 0; f < 4; f++)
        {
            for (int i = 0; i < ZOOM_KEYS; i++)
            {
                const int ip = (i - 1 + ZOOM_KEYS) % ZOOM_KEYS;
                const int in = (i + 1) % ZOOM_KEYS;

                const double dPrev = (_values[i][f] - _values[ip][f]) / (double)_durations[ip];
                const double dNext = (_values[in][f] - _values[i][f]) / (double)_durations[i];

                double m = 0.0;
                if ((dPrev > 0.0 && dNext > 0.0) || (dPrev < 0.0 && dNext < 0.0))
                {
                    const double w1 = 2.0 * _durations[i] + _durations[ip];
                    const double w2 = _durations[i] + 2.0 * _durations[ip];
                    m = (w1 + w2) / ((w1 / dPrev) + (w2 / dNext));

                    const double lim = 3.0 * fmin(fabs(dPrev), fabs(dNext));
                    if (m > lim)
                    {
                        m = lim;
                    }
                    else if (m < -lim)
                    {
                        m = -lim;
                    }
                }
                _tangents[i][f] = m;
            }
        }
    }

    // One closed loop: seahorse valley -> triple spiral -> Feigenbaum point ->
    // mini Mandelbrot -> wide. Frames are budgeted for the dives, not the
    // travel: most of the loop is spent zooming, and the pans between
    // destinations are short hops at a wider scale where motion reads clearly.
    // A repeated key is a hold (equal endpoints force a flat, frozen segment).
    const zoom_key_t _keys[ZOOM_KEYS] = {
        {-0.500000f, 0.000000f, 3.00000f, 105, 16}, // wide establishing view
        {-0.700000f, 0.140000f, 1.10000f, 135, 9},  // quick pull towards seahorse
        {-0.750000f, 0.110000f, 0.40000f, 165, 12}, // seahorse valley approach
        {-0.745000f, 0.120000f, 0.12000f, 210, 16}, {-0.746900f, 0.135200f, 0.02000f, 300, 20},
        {-0.746975f, 0.135175f, 0.00400f, 420, 10},                                             // deep spiral, short hold
        {-0.746975f, 0.135175f, 0.00400f, 420, 14},                                             // (hold, then pull out)
        {-0.746900f, 0.135200f, 0.02000f, 300, 10}, {-0.750000f, 0.110000f, 0.40000f, 165, 10}, // back out
        {-0.450000f, 0.400000f, 1.20000f, 130, 10},                                             // north, wide
        {-0.100000f, 0.700000f, 0.90000f, 140, 9},                                              // towards triple spiral
        {-0.088000f, 0.654000f, 0.12000f, 220, 14},                                             // triple spiral approach
        {-0.064000f, 0.660000f, 0.05000f, 280, 8},                                              // close, short hold
        {-0.064000f, 0.660000f, 0.05000f, 280, 12},                                             // (hold, then pull out)
        {-0.088000f, 0.654000f, 0.12000f, 220, 10}, {-0.600000f, 0.550000f, 1.30000f, 125, 10}, // west, wide
        {-1.100000f, 0.250000f, 1.00000f, 120, 9},                                              // towards the real axis
        {-1.404155f, 0.000000f, 0.06000f, 280, 12},                                             // Feigenbaum point approach
        {-1.404155f, 0.000000f, 0.01200f, 380, 8},                                              // close, short hold
        {-1.404155f, 0.000000f, 0.01200f, 380, 12},                                             // (hold, then pull out)
        {-1.550000f, 0.000000f, 0.08000f, 280, 10}, {-1.768800f, 0.001500f, 0.04000f, 300, 12}, // mini Mandelbrot approach
        {-1.768800f, 0.001500f, 0.02500f, 320, 8},                                              // close, short hold
        {-1.768800f, 0.001500f, 0.02500f, 320, 26},                                             // (hold, then reveal wide)
    };

    static_assert((sizeof(_keys) / sizeof(_keys[0])) == ZOOM_KEYS, "ZOOM_KEYS must match the key table");
};

#endif
