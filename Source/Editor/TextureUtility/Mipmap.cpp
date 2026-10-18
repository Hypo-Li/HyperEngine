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

    float3 cubeGetDirection(CubeFace face, float x, float y, uint32_t dim)
    {
        const float scale = 2.0f / dim;
        const float cx = x * scale - 1.0f; // -1 to 1, left to right
        const float cy = y * scale - 1.0f; // -1 to 1, top to bottom (origin top-left)

        float3 dir;
        switch (face)
        {
        case CubeFace::PX: dir = float3(1.0f, -cy, -cx); break;
        case CubeFace::NX: dir = float3(-1.0f, -cy, cx); break;
        case CubeFace::PY: dir = float3(cx, 1.0f, cy); break;
        case CubeFace::NY: dir = float3(cx, -1.0f, -cy); break;
        case CubeFace::PZ: dir = float3(cx, -cy, 1.0f); break;
        case CubeFace::NZ: dir = float3(-cx, -cy, -1.0f); break;
        }

        return glm::normalize(dir);
    }

    CubeAddress cubeGetAddressFor(const float3& r)
    {
        CubeAddress addr;
        const float rx = std::abs(r.x);
        const float ry = std::abs(r.y);
        const float rz = std::abs(r.z);

        float sc, tc, ma;

        if (rx >= ry && rx >= rz)
        {
            ma = 1.0f / rx;
            if (r.x >= 0.0f)
            {
                addr.face = CubeFace::PX;
                sc = -r.z;
                tc = -r.y;
            }
            else
            {
                addr.face = CubeFace::NX;
                sc = r.z;
                tc = -r.y;
            }
        }
        else if (ry >= rx && ry >= rz)
        {
            ma = 1.0f / ry;
            if (r.y >= 0.0f)
            {
                addr.face = CubeFace::PY;
                sc = r.x;
                tc = r.z;
            }
            else
            {
                addr.face = CubeFace::NY;
                sc = r.x;
                tc = -r.z;
            }
        }
        else
        {
            ma = 1.0f / rz;
            if (r.z >= 0.0f)
            {
                addr.face = CubeFace::PZ;
                sc = r.x;
                tc = -r.y;
            }
            else
            {
                addr.face = CubeFace::NZ;
                sc = -r.x;
                tc = -r.y;
            }
        }

        addr.s = (sc * ma + 1.0f) * 0.5f;
        addr.t = (tc * ma + 1.0f) * 0.5f;
        return addr;
    }

}
