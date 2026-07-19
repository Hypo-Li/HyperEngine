#pragma once
#include <Core/Math/Vector.h>
#include <Core/Math/Matrix.h>

namespace hyp
{
    static constexpr uint32_t kMaxFramesInFlight = 2;
    template <typename T>
    using InFlightArray = std::array<T, kMaxFramesInFlight>;

    struct WorldCoord
    {
        int4 tile{ 0 };
        // range: [-halfTileSize, halfTileSize)
        float4 offset{ 0 };

        static constexpr double TILE_SIZE = 1024.0;
        static constexpr double HALF_TILE_SIZE = TILE_SIZE * 0.5;
        static constexpr double INV_TILE_SIZE = 1.0 / TILE_SIZE;

        inline void fromDouble3(const double3& coord)
        {
            for (int i = 0; i < 3; ++i)
            {
                double tileF = std::floor((coord[i] + HALF_TILE_SIZE) * INV_TILE_SIZE);
                this->tile[i] = static_cast<int32_t>(tileF);
                this->offset[i] = static_cast<float>(coord[i] - tileF * TILE_SIZE);
            }
        }

        inline double3 toDouble3() const
        {
            double3 result;
            for (int i = 0; i < 3; ++i)
            {
                result[i] = static_cast<double>(tile[i]) * TILE_SIZE + offset[i];
            }
            return result;
        }
    };

    struct WorldMatrix
    {
        float3x4 basisAndOffset;
        int4 tile;

        inline WorldCoord getTranslation() const noexcept
        {
            WorldCoord result;
            result.offset = float4(
                basisAndOffset[0].w,
                basisAndOffset[1].w,
                basisAndOffset[2].w,
                0.0f
            );
            result.tile = tile;
            return result;
        }

        inline float3x3 getBasis() const noexcept
        {
            return float3x3(
                float3(basisAndOffset[0]),
                float3(basisAndOffset[1]),
                float3(basisAndOffset[2])
            );
        }

        inline void fromDouble4x4(const double4x4& matrix)
        {
            basisAndOffset = float4x4(matrix);
            WorldCoord translation;
            translation.fromDouble3(matrix[3]);
            basisAndOffset[0].w = translation.offset.x;
            basisAndOffset[1].w = translation.offset.y;
            basisAndOffset[2].w = translation.offset.z;
            tile = translation.tile;
        }
    };
}
