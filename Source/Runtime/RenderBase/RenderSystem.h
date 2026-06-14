#pragma once
#include <thread>
#include "RenderFrontend.h"
#include "RenderBackend.h"

namespace hyp
{
    class RenderSystem
    {
    public:
        RenderSystem(vk::Instance& instance, vk::Device& device, const Window& window) :
            mBackend(instance, device, window),
            mFrontend(mBackend)
        {
        }

        RenderFrontend& getFrontend() noexcept { return mFrontend; }
        RenderBackend& getBackend() noexcept { return mBackend; }


    private:
        RenderFrontend mFrontend;
        RenderBackend mBackend;
    };
}
