#include "RenderSystem.h"

#include <cassert>

namespace hyp
{
    RenderSystem::RenderSystem(
        const Window& window,
        rhi::Backend rhiBackend,
        const std::filesystem::path& shaderDir,
        const std::filesystem::path& resourceDir
    )
    {
        mBackend = std::make_unique<RenderBackend>(window, rhiBackend, shaderDir, resourceDir);
        mFrontend = std::make_unique<RenderFrontend>(*mBackend);
        mRendererContext = std::make_unique<RendererContext>(*mBackend);
    }

    RenderSystem::~RenderSystem()
    {
        assert(mShutdown && "RenderSystem::shutdown() must be called before destruction");
        mBackend->drainPendingWork();
    }

    RenderBackend& RenderSystem::getBackend() const noexcept
    {
        assert(mBackend);
        return *mBackend;
    }

    RenderFrontend& RenderSystem::getFrontend() const noexcept
    {
        assert(mFrontend);
        return *mFrontend;
    }

    RendererContext& RenderSystem::getRendererContext() const noexcept
    {
        assert(mRendererContext);
        return *mRendererContext;
    }

    void RenderSystem::shutdown()
    {
        if (mShutdown)
            return;
        mShutdown = true;

        mFrontend->shutdown();
        mBackend->shutdown();
    }
}
