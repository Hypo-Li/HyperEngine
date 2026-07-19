#include <chrono>

#include "Engine.h"
#include "Context.h"

namespace hyp
{
    Engine::Engine()
    {
        mWindow = std::make_unique<Window>(1280, 720, "Hyper Engine");

        auto& context = Context::get();

        mInputSystem = std::make_unique<InputSystem>(*mWindow);
        mSampleStateManager = std::make_unique<SampleStateManager>();
        mRenderSystem = std::make_unique<RenderSystem>(*mWindow);
        mRendererContext = std::make_unique<RendererContext>(mRenderSystem->getBackend());
        mAssetManager = std::make_unique<AssetManager>();
        mAssetManager->initialize(ENGINE_ASSET_DIR, PROJECT_ASSET_DIR);

        context.setInputSystem(mInputSystem.get());
        context.setSampleStateManager(mSampleStateManager.get());
        context.setRenderFrontend(&mRenderSystem->getFrontend());
        context.setRendererContext(mRendererContext.get());
        context.setAssetManager(mAssetManager.get());
        context.setRenderTagRegistry(&mRenderSystem->getBackend().getRenderTagRegistry());
    }

    Engine::~Engine()
    {
        mScene = nullptr;
        mAssetManager = nullptr;
        mSampleStateManager = nullptr;
        mRenderSystem->getBackend().finishRenderThread();
        mRendererContext = nullptr;
        mRenderSystem = nullptr;
    }

    void Engine::run()
    {
        auto beginTime = std::chrono::high_resolution_clock::now();
        auto prevTime = beginTime;
        while (!mWindow->shouldClose())
        {
            auto currTime = std::chrono::high_resolution_clock::now();
            float deltaTime = std::chrono::duration<float>(currTime - prevTime).count();
            prevTime = currTime;
            tick(deltaTime);
        }
    }

    void Engine::tick(float deltaTime)
    {
        mInputSystem->tick(deltaTime);

        auto& renderFrontend = mRenderSystem->getFrontend();
        renderFrontend.beginFrame();

        if (onPreTick)
        {
            onPreTick(deltaTime);
        }

        if (mScene)
        {
            mScene->tick(deltaTime);
        }

        if (onPreRender)
        {
            onPreRender();
        }

        mTime += deltaTime;
        renderFrontend.render(mTime, deltaTime);
        renderFrontend.endFrame();
    }

}
