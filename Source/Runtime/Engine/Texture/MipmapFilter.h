#pragma once

#include <cmath>

namespace hyp
{
    enum class MipmapFilter
    {
        Box,
        Lanczos,
    };

    struct FilterKernel
    {
        float (*weight)(float t);
        float radius;
    };

    inline FilterKernel getFilterKernel(MipmapFilter filter)
    {
        switch (filter)
        {
        case MipmapFilter::Box:
            return FilterKernel {
                [](float t) { return t <= 0.5f ? 1.0f : 0.0f; },
                1.0f
            };
        case MipmapFilter::Lanczos:
            return FilterKernel {
                [](float t) {
                    if (t >= 1.0f) return 0.0f;
                    constexpr float pi = 3.14159265358979323846f;
                    auto sinc = [](float x) {
                        if (x <= 0.00001f) return 1.0f;
                        return std::sin(pi * x) / (pi * x);
                    };
                    return sinc(t) * sinc(t);
                },
                1.0f
            };
        }
        return getFilterKernel(MipmapFilter::Box);
    }
}
