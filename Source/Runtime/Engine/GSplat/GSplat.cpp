#include "GSplat.h"

namespace hyp
{
    REGISTER_CLASS(GSplat)

    GSplat::GSplat()
    {

    }

    GSplat::~GSplat() noexcept
    {

    }

    void GSplat::setSplatData(std::vector<SplatData>&& data)
    {
        mSplatData = std::move(data);
        mSplatCount = static_cast<uint32_t>(mSplatData.size());
        for (auto& splat : mSplatData)
        {
            mBoundingBox += splat.position;
        }
    }

    template <> Type* Reflection::createType<SplatData>()
    {
        auto s = new TStructure<SplatData>("SplatData");
        s->addProperty("Position", &SplatData::position);
        s->addProperty("Scale", &SplatData::scale);
        s->addProperty("Color", &SplatData::color);
        s->addProperty("Rotation", &SplatData::rotation);
        return s;
    }

    template <> Type* Reflection::createType<GSplat>()
    {
        auto c = new TClass<GSplat>("GSplat");
        return c;
    }
}
