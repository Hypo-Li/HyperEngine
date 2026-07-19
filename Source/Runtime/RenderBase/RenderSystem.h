#pragma once
#include "RenderFrontend.h"
#include "RenderBackend.h"

namespace hyp
{
    class RenderSystem
    {
    public:
        RenderSystem(const Window& window) :
            mBackend(window),
            mFrontend(mBackend)
        {
        }

        RenderFrontend& getFrontend() noexcept { return mFrontend; }
        RenderBackend& getBackend() noexcept { return mBackend; }


    private:
        RenderBackend mBackend;
        RenderFrontend mFrontend;
    };
}
