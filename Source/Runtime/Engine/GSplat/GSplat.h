#pragma once
#include <Core/Object/Object.h>
#include <Core/Math/Vector.h>
#include <Core/Math/Box.h>

namespace hyp
{
    struct SplatData
    {
        float3 position;
        float3 scale;
        // rgba
        std::array<uint8_t, 4> color;
        // wxyz
        std::array<uint8_t, 4> rotation;
    };

    class GSplat : public Object
    {
        REFLECT_CLASS(GSplat)
    public:
        GSplat();

        ~GSplat() noexcept;

        void setSplatData(std::vector<SplatData>&& data);

        std::span<const SplatData> getSplatData() const noexcept
        {
            return mSplatData;
        }

        const Boxf& getBoundingBox() const noexcept
        {
            return mBoundingBox;
        }

    private:
        std::vector<SplatData> mSplatData;
        uint32_t mSplatCount = 0;
        Boxf mBoundingBox;
    };

    template <> Type* Reflection::createType<SplatData>();

    template <> Type* Reflection::createType<GSplat>();
}
