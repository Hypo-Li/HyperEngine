#include "Mipmap.h"

namespace hyp
{
    float ColorSpace::srgbToLinear(uint8_t v)
    {
        static const auto lut = []() {
            std::array<float, 256> table;
            for (int i = 0; i < 256; i++)
            {
                float s = i / 255.0f;
                table[i] = (s <= 0.04045f) ? (s / 12.92f) : std::pow((s + 0.055f) / 1.055f, 2.4f);
            }
            return table;
        }();
        return lut[v];
    }

    uint8_t ColorSpace::linearToSrgb(float v)
    {
        static const auto lut = []() {
            std::array<uint8_t, 1025> table; // 1025 大小，方便处理 1.0f 的边界
            for (int i = 0; i <= 1024; i++)
            {
                float l = i / 1024.0f;
                float s = (l <= 0.0031308f) ? (l * 12.92f) : (1.055f * std::pow(l, 1.0f / 2.4f) - 0.055f);
                table[i] = static_cast<uint8_t>(std::round(std::clamp(s, 0.0f, 1.0f) * 255.0f));
            }
            return table;
        }();

        // 安全查表
        int index = static_cast<int>(std::round(std::clamp(v, 0.0f, 1.0f) * 1024.0f));
        return lut[index];
    }

    uint16_t FloatConverter::float32ToFloat16(float v)
    {
        uint32_t f32i;
        std::memcpy(&f32i, &v, sizeof(uint32_t));

        uint32_t s = (f32i >> 16) & 0x8000;          // 符号位
        int32_t e = ((f32i >> 23) & 0xFF) - 112;     // 指数位并重新偏移
        uint32_t m = f32i & 0x7FFFFF;                // 尾数位

        if (e <= 0) {
            if (e < -10) return (uint16_t)s;         // 太小，转为 0

            // 处理非规格化数 (Denormalized)
            m = (m | 0x800000) >> (1 - e);
            return (uint16_t)(s | (m >> 13));
        }
        else if (e == 143) {
            if (m == 0) return (uint16_t)(s | 0x7C00); // 无穷大
            return (uint16_t)(s | 0x7C00 | (m >> 13)); // NaN
        }
        else {
            if (e > 30) return (uint16_t)(s | 0x7C00); // 溢出转为无穷大
            return (uint16_t)(s | (e << 10) | (m >> 13));
        }
    }

    float FloatConverter::float16ToFloat32(uint16_t v)
    {
        uint32_t s = (v >> 15) & 0x0001;
        uint32_t e = (v >> 10) & 0x001F;
        uint32_t m = v & 0x03FF;

        uint32_t f32i;
        if (e == 0) {
            if (m == 0) {
                f32i = s << 31; // 0
            }
            else {
                // 非规格化数转规格化
                while (!(m & 0x0400)) {
                    m <<= 1;
                    e--;
                }
                e++;
                f32i = (s << 31) | ((e + 112) << 23) | ((m & ~0x0400) << 13);
            }
        }
        else if (e == 31) {
            f32i = (s << 31) | (0xFF << 23) | (m << 13); // NaN 或 无穷大
        }
        else {
            f32i = (s << 31) | ((e + 112) << 23) | (m << 13);
        }

        float f32;
        std::memcpy(&f32, &f32i, sizeof(uint32_t));
        return f32;
    }

}
