#include "Float16.h"

#include <bit>

namespace hyp
{
    float16 float32ToFloat16(float f)
    {
        uint32_t f32i = std::bit_cast<uint32_t>(f);

        uint32_t s = (f32i >> 16) & 0x8000;          // 符号位
        int32_t e = ((f32i >> 23) & 0xFF) - 112;     // 指数位并重新偏移
        uint32_t m = f32i & 0x7FFFFF;                // 尾数位

        if (e <= 0)
        {
            if (e < -10) return (uint16_t)s;         // 太小，转为 0

            // 处理非规格化数 (Denormalized)
            m = (m | 0x800000) >> (1 - e);
            return (uint16_t)(s | (m >> 13));
        }
        else if (e == 143)
        {
            if (m == 0) return (uint16_t)(s | 0x7C00); // 无穷大
            return (uint16_t)(s | 0x7C00 | (m >> 13)); // NaN
        }
        else
        {
            if (e > 30) return (uint16_t)(s | 0x7C00); // 溢出转为无穷大
            return (uint16_t)(s | (e << 10) | (m >> 13));
        }
    }

    float float16ToFloat32(float16 f)
    {
        uint32_t s = (f >> 15) & 0x0001;
        uint32_t e = (f >> 10) & 0x001F;
        uint32_t m = f & 0x03FF;

        uint32_t f32i;
        if (e == 0)
        {
            if (m == 0)
            {
                f32i = s << 31; // 0
            }
            else
            {
                // 非规格化数转规格化
                while (!(m & 0x0400))
                {
                    m <<= 1;
                    e--;
                }
                e++;
                f32i = (s << 31) | ((e + 112) << 23) | ((m & ~0x0400) << 13);
            }
        }
        else if (e == 31)
        {
            f32i = (s << 31) | (0xFF << 23) | (m << 13); // NaN 或 无穷大
        }
        else
        {
            f32i = (s << 31) | ((e + 112) << 23) | (m << 13);
        }

        float f32 = std::bit_cast<float>(f32i);
        return f32;
    }
}
