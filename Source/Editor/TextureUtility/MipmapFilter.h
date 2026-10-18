#pragma once

#include <cmath>

namespace hyp
{
    enum class MipmapFilter
    {
        Box,
        Lanczos,
    };

    // weight() 按 type 用 switch 派发而不是存函数指针——resample1D 内层循环每个采样点都要
    // 调一次, 函数指针会挡住内联, switch 分支在 kernel 按值传入且 type 循环不变的情况下能被
    // 编译器内联/特化, 数学结果跟原来逐字节一致
    struct FilterKernel
    {
        MipmapFilter type;
        float radius;

        float weight(float t) const
        {
            switch (type)
            {
            case MipmapFilter::Lanczos:
            {
                if (t >= 1.0f) return 0.0f;
                constexpr float pi = 3.14159265358979323846f;
                auto sinc = [](float x) {
                    if (x <= 0.00001f) return 1.0f;
                    return std::sin(pi * x) / (pi * x);
                    };
                const float s = sinc(t);
                return s * s;
            }
            case MipmapFilter::Box:
            default:
                return t <= 0.5f ? 1.0f : 0.0f;
            }
        }
    };

    inline FilterKernel getFilterKernel(MipmapFilter filter)
    {
        return FilterKernel{ filter, 1.0f };
    }
}
