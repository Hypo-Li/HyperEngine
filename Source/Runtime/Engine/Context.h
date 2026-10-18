#pragma once
#include <cassert>

namespace hyp
{
    class Engine;
    class InputSystem;
    class RenderFrontend;
    class RendererContext;
    class AssetManager;
    class RenderTagRegistry;
    class Config;
    class PhysicsSystem;

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

        Config& getConfig() const noexcept
        {
            assert(mConfig);
            return *mConfig;
        }

        PhysicsSystem& getPhysicsSystem() const noexcept
        {
            assert(mPhysicsSystem);
            return *mPhysicsSystem;
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

        void setEnvironment(Environment environment) noexcept
        {
            mEnvironment = environment;
        }

        void setInputSystem(InputSystem* inputSystem)
        {
            mInputSystem = inputSystem;
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

        void setConfig(Config* config)
        {
            mConfig = config;
        }

        void setPhysicsSystem(PhysicsSystem* physicsSystem)
        {
            mPhysicsSystem = physicsSystem;
        }

    private:
        InputSystem* mInputSystem = nullptr;
        RenderFrontend* mRenderFrontend = nullptr;
        RendererContext* mRendererContext = nullptr;
        AssetManager* mAssetManager = nullptr;
        RenderTagRegistry* mRenderTagRegistry = nullptr;
        Config* mConfig = nullptr;
        PhysicsSystem* mPhysicsSystem = nullptr;
        Environment mEnvironment = Environment::Standalone;
    };
}
