#pragma once
#include "Framework/Scene.h"
#include "Texture/SampleState.h"
#include "Input/InputSystem.h"

#include <Core/Asset/AssetManager.h>
#include <Platform/Window.h>
#include <RenderBase/RenderSystem.h>
#include <Renderer/RendererContext.h>

namespace hyp
{
    class Engine
    {
    public:
        Engine();

        ~Engine();

        template <typename T>
        T& get() noexcept;

        template <>
        RenderSystem& get() noexcept
        {
            return *mRenderSystem;
        }

        void setScene(Scene* scene)
        {
            mScene = scene;
        }

        void setPreTickCallback(std::function<void(float)> callback)
        {
            onPreTick = std::move(callback);
        }

        void setPreRenderCallback(std::function<void()> callback)
        {
            onPreRender = std::move(callback);
        }

        void run();

    private:
        void tick(float deltaTime);

    private:
        std::unique_ptr<Window> mWindow;
        std::unique_ptr<InputSystem> mInputSystem;
        std::unique_ptr<SampleStateManager> mSampleStateManager;
        std::unique_ptr<RenderSystem> mRenderSystem;
        std::unique_ptr<RendererContext> mRendererContext;
        std::unique_ptr<AssetManager> mAssetManager;
        TRef<Scene> mScene;

        bool mRunning = true;
        double mTime = 0.0;

        std::function<void(float)> onPreTick;
        std::function<void()> onPreRender;
    };
}
