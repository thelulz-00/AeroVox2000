#pragma once
#include <cmath>

static constexpr double kBiquadPi = 3.14159265358979323846;

// Small direct-form-II-transposed biquad with RBJ cookbook designs.
struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    float z1 = 0, z2 = 0;

    inline float process (float x) noexcept
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    void reset() noexcept { z1 = z2 = 0; }

    void setBandPass (double fs, double f, double q) noexcept
    {
        const double w0 = 2.0 * kBiquadPi * f / fs, alpha = std::sin (w0) / (2.0 * q), c = std::cos (w0);
        set (alpha, 0.0, -alpha, 1.0 + alpha, -2.0 * c, 1.0 - alpha);
    }

    void setHighPass (double fs, double f, double q) noexcept
    {
        const double w0 = 2.0 * kBiquadPi * f / fs, alpha = std::sin (w0) / (2.0 * q), c = std::cos (w0);
        set ((1.0 + c) * 0.5, -(1.0 + c), (1.0 + c) * 0.5, 1.0 + alpha, -2.0 * c, 1.0 - alpha);
    }

    void setHighShelf (double fs, double f, double q, double gainDb) noexcept
    {
        const double A = std::pow (10.0, gainDb / 40.0);
        const double w0 = 2.0 * kBiquadPi * f / fs, alpha = std::sin (w0) / (2.0 * q), c = std::cos (w0);
        const double sa = 2.0 * std::sqrt (A) * alpha;
        set (A * ((A + 1) + (A - 1) * c + sa),
             -2.0 * A * ((A - 1) + (A + 1) * c),
             A * ((A + 1) + (A - 1) * c - sa),
             (A + 1) - (A - 1) * c + sa,
             2.0 * ((A - 1) - (A + 1) * c),
             (A + 1) - (A - 1) * c - sa);
    }

    // RBJ peaking bell
    void setPeak (double fs, double f, double q, double gainDb) noexcept
    {
        const double A = std::pow (10.0, gainDb / 40.0);
        const double w0 = 2.0 * kBiquadPi * f / fs, alpha = std::sin (w0) / (2.0 * q), c = std::cos (w0);
        set (1.0 + alpha * A, -2.0 * c, 1.0 - alpha * A, 1.0 + alpha / A, -2.0 * c, 1.0 - alpha / A);
    }

    // magnitude response in dB at frequency f
    double magnitudeDb (double fs, double f) const noexcept
    {
        const double w = 2.0 * kBiquadPi * f / fs;
        const double c1 = std::cos (w), s1 = std::sin (w), c2 = std::cos (2 * w), s2 = std::sin (2 * w);
        const double nr = b0 + b1 * c1 + b2 * c2, ni = -(b1 * s1 + b2 * s2);
        const double dr = 1.0 + a1 * c1 + a2 * c2, di = -(a1 * s1 + a2 * s2);
        return 10.0 * std::log10 ((nr * nr + ni * ni) / (dr * dr + di * di) + 1e-30);
    }

private:
    void set (double nb0, double nb1, double nb2, double na0, double na1, double na2) noexcept
    {
        b0 = (float) (nb0 / na0); b1 = (float) (nb1 / na0); b2 = (float) (nb2 / na0);
        a1 = (float) (na1 / na0); a2 = (float) (na2 / na0);
    }
};
