#pragma once
#include <cstdint>

namespace hyp
{
    using float16 = uint16_t;

    float16 float32ToFloat16(float f);

    float float16ToFloat32(float16 h);
}
