#pragma once

#include <filesystem>
#include <memory>

#include <Platform/Window.h>
#include <RenderBase/RenderBackend.h>
#include <RenderBase/RenderFrontend.h>
#include <Renderer/RendererContext.h>

namespace hyp
{
    class RenderSystem
    {
    public:
        RenderSystem(
            const Window& window,
            rhi::Backend rhiBackend,
            const std::filesystem::path& shaderDir,
            const std::filesystem::path& resourceDir
        );

        ~RenderSystem();

        RenderSystem(const RenderSystem&) = delete;
        RenderSystem& operator=(const RenderSystem&) = delete;

        RenderBackend& getBackend() const noexcept;

        RenderFrontend& getFrontend() const noexcept;

        RendererContext& getRendererContext() const noexcept;

        void shutdown();

    private:
        std::unique_ptr<RenderBackend> mBackend;
        std::unique_ptr<RenderFrontend> mFrontend;
        std::unique_ptr<RendererContext> mRendererContext;
        bool mShutdown = false;
    };
}
