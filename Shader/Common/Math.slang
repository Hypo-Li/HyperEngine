#pragma once

static constexpr float PI = 3.1415926535897932;
static constexpr float RAD_TO_DEG = 180.0 / PI;
static constexpr float DEG_TO_RAD = PI / 180.0;

__generic<typename T>
T pow2(T x) where T : IArithmetic
{
    return x * x;
}
