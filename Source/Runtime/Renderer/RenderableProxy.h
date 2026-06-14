#pragma once
#include <RenderBase/RenderProxy.h>

namespace hyp
{
    class RenderableProxy : public RenderProxy
    {
    public:
        RenderableProxy() = default;

        void setVisible(bool visible) noexcept { mVisible = visible; }

        bool isVisible() const noexcept { return mVisible; }

        void setInstanceIndex(uint32_t instanceIndex) noexcept
        {
            mInstanceIndex = instanceIndex;
        }

        uint32_t getInstanceIndex() const noexcept
        {
            return mInstanceIndex;
        }

    private:
        bool mVisible = true;
        uint32_t mInstanceIndex = 0;
    };
}
