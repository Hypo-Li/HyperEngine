#include <chrono>

#include "Engine.h"
#include "Context.h"

#include <RenderBase/Vulkan/Context.h>

namespace hyp
{
    Engine::Engine()
    {
        mWindow = std::make_unique<Window>(1280, 720, "Hyper Engine");

        auto& context = Context::get();

        auto& instance = vk::Context::get().getInstance();
        auto& device = vk::Context::get().getDevice();
        mInputSystem = std::make_unique<InputSystem>(*mWindow);
        mSampleStateManager = std::make_unique<SampleStateManager>();
        mRenderSystem = std::make_unique<RenderSystem>(instance, device, *mWindow);
        mSceneRendererContext = std::make_unique<SceneRendererContext>(mRenderSystem->getBackend());

        context.setInputSystem(mInputSystem.get());
        context.setSampleStateManager(mSampleStateManager.get());
        context.setRenderFrontend(&mRenderSystem->getFrontend());
        context.setSceneRendererContext(mSceneRendererContext.get());

        mScene = createObject<Scene>(nullptr);
    }

    Engine::~Engine()
    {
        mScene = nullptr;
        mSampleStateManager = nullptr;
        mRenderSystem->getBackend().finishRenderThread();
        mSceneRendererContext = nullptr;
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

        mScene->tick(deltaTime);

        if (onPreRender)
        {
            onPreRender();
        }

        mTime += deltaTime;
        renderFrontend.render(mTime, deltaTime);
        renderFrontend.endFrame();
    }

}
