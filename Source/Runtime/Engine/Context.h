#pragma once
#include <cassert>

namespace hyp
{
    class Engine;
    class InputSystem;
    class SampleStateManager;
    class RenderFrontend;
    class RendererContext;
    class AssetManager;
    class RenderTagRegistry;

    class Context
    {
        friend class Engine;
    public:
        static Context& get();

        Context(const Context&) = delete;
        Context& operator=(const Context&) = delete;

        ~Context();

        InputSystem& getInputSystem() const noexcept
        {
            assert(mInputSystem);
            return *mInputSystem;
        }

        SampleStateManager& getSampleStateManager() const noexcept
        {
            assert(mSampleStateManager);
            return *mSampleStateManager;
        }

        RenderFrontend& getRenderFrontend() const noexcept
        {
            assert(mRenderFrontend);
            return *mRenderFrontend;
        }

        RendererContext& getRendererContext() const noexcept
        {
            assert(mRendererContext);
            return *mRendererContext;
        }

        AssetManager& getAssetManager() const noexcept
        {
            assert(mAssetManager);
            return *mAssetManager;
        }

        RenderTagRegistry& getRenderTagRegistry() const noexcept
        {
            assert(mRenderTagRegistry);
            return *mRenderTagRegistry;
        }

        enum class Environment
        {
            Editor,
            Standalone,
        };

        Environment getEnvironment() const noexcept
        {
            return mEnvironment;
        }

        bool isEditor() const noexcept
        {
            return mEnvironment == Environment::Editor;
        }

    private:
        Context();

        void setInputSystem(InputSystem* inputSystem)
        {
            mInputSystem = inputSystem;
        }

        void setSampleStateManager(SampleStateManager* sampleStateManager)
        {
            mSampleStateManager = sampleStateManager;
        }

        void setRenderFrontend(RenderFrontend* renderFrontend)
        {
            mRenderFrontend = renderFrontend;
        }

        void setRendererContext(RendererContext* rendererContext)
        {
            mRendererContext = rendererContext;
        }

        void setAssetManager(AssetManager* assetManager)
        {
            mAssetManager = assetManager;
        }

        void setRenderTagRegistry(RenderTagRegistry* renderTagRegistry)
        {
            mRenderTagRegistry = renderTagRegistry;
        }

    private:
        InputSystem* mInputSystem = nullptr;
        SampleStateManager* mSampleStateManager = nullptr;
        RenderFrontend* mRenderFrontend = nullptr;
        RendererContext* mRendererContext = nullptr;
        AssetManager* mAssetManager = nullptr;
        RenderTagRegistry* mRenderTagRegistry = nullptr;
        Environment mEnvironment = Environment::Standalone;
    };
}
