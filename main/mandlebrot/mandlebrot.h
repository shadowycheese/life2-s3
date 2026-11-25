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
#define ZOOM_KEYS 25

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

        preparePath();
    }

    uint16_t iterations(int x, int y)
    {
        return _grid[(y * _w) + x];
    }

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

        return (uint16_t)loop;
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
        const double logScale = h00 * _values[k][2] + h10 * h * _tangents[k][2] + h01 * _values[kn][2] + h11 * h * _tangents[kn][2];
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

    // One closed loop: seahorse valley -> spiral -> triple spiral ->
    // elephant valley -> lower boundary -> mini Mandelbrot -> wide.
    // Each zoom-in is paired with a zoom-out and travel legs stay at a
    // moderate scale so the boundary is always visible (never all black).
    const zoom_key_t _keys[ZOOM_KEYS] = {
        {-0.500000f, 0.000000f, 3.00000f,  90, 24}, // wide establishing view
        {-0.750000f, 0.110000f, 0.50000f, 135, 30}, // seahorse valley approach
        {-0.745000f, 0.110000f, 0.15000f, 180, 26},
        {-0.744000f, 0.160000f, 0.03000f, 238, 26},
        {-0.746975f, 0.135175f, 0.00500f, 275, 28}, // deep spiral
        {-0.744000f, 0.160000f, 0.03000f, 238, 26},
        {-0.750000f, 0.110000f, 0.50000f, 135, 30}, // back out
        {-0.450000f, 0.400000f, 0.50000f, 150, 28}, // travel north along boundary
        {-0.125000f, 0.744000f, 0.50000f, 160, 28},
        {-0.088000f, 0.654000f, 0.12000f, 206, 26}, // triple spiral approach
        {-0.058000f, 0.661500f, 0.02500f, 238, 28}, // triple spiral close
        {-0.088000f, 0.654000f, 0.12000f, 206, 26},
        {-0.125000f, 0.744000f, 0.50000f, 160, 28}, // back out
        { 0.350000f, 0.350000f, 0.50000f, 155, 28}, // travel around cardioid (outside)
        { 0.250000f, 0.000000f, 0.50000f, 145, 28}, // cusp
        { 0.320000f, 0.010000f, 0.08000f, 211, 26}, // elephant valley
        { 0.250000f, 0.000000f, 0.50000f, 145, 28},
        { 0.350000f,-0.350000f, 0.50000f, 150, 26}, // travel south
        { 0.150000f,-0.600000f, 0.50000f, 155, 28},
        {-0.150000f,-0.600000f, 0.50000f, 155, 28},
        {-0.450000f,-0.550000f, 0.50000f, 150, 28},
        {-0.750000f,-0.110000f, 0.20000f, 170, 26}, // lower seahorse
        {-0.900000f,-0.450000f, 0.70000f, 150, 28}, // bypass below period-2 bulb
        {-1.200000f, 0.000000f, 1.00000f, 110, 30}, // antenna
        {-1.768800f, 0.001500f, 0.08000f, 220, 42}, // mini Mandelbrot (then back to wide)
    };
};

#endif
