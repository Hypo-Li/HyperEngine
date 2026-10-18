#include <chrono>

#include "Engine.h"
#include "Context.h"
#include <Core/Common/LogSystem.h>
#include <Platform/Platform.h>
#include <RenderBase/RenderBackend.h>
#include <RenderBase/RenderFrontend.h>

namespace hyp
{
    Engine::Engine(Context::Environment environment)
    {
        // 尽量早启动日志——后面任何一步(Config 解析、Window/RenderSystem 初始化)失败都想
        // 有日志能记下来。logDir 用可执行文件所在目录而不是当前工作目录, 跟 Config 里各个
        // 资源目录路径解析同一个原则(不依赖进程启动时的工作目录设置)。
        LogSystem::get().init(Platform::getExecutableDir() / "Logs");
#ifndef NDEBUG
        LogSystem::get().setLogLevel(LogLevel::Debug);
#endif

        mConfig = std::make_unique<Config>();

        auto& context = Context::get();
        context.setEnvironment(environment);
        context.setConfig(mConfig.get());

        mWindow = std::make_unique<Window>(
            static_cast<int>(mConfig->getWindowWidth()),
            static_cast<int>(mConfig->getWindowHeight()),
            mConfig->getWindowTitle()
        );

        mInputSystem = std::make_unique<InputSystem>(*mWindow);
        mRenderSystem = std::make_unique<RenderSystem>(*mWindow, mConfig->getRHIBackend(), mConfig->getShaderDir(), mConfig->getResourceDir());
        mAssetManager = std::make_unique<AssetManager>();
        mAssetManager->initialize(mConfig->getEngineAssetDir(), mConfig->getProjectAssetDir());

        mPhysicsSystem = std::make_unique<PhysicsSystem>();
        mPhysicsSystem->initialize();
        mPhysicsSystem->setLayerSettings(mConfig->getPhysicsLayerSettings());

        context.setInputSystem(mInputSystem.get());
        context.setRenderFrontend(&mRenderSystem->getFrontend());
        context.setRendererContext(&mRenderSystem->getRendererContext());
        context.setAssetManager(mAssetManager.get());
        context.setRenderTagRegistry(&mRenderSystem->getBackend().getRenderTagRegistry());
        context.setPhysicsSystem(mPhysicsSystem.get());
    }

    Engine::~Engine()
    {
        shutdown();

        auto& context = Context::get();
        mAssetManager.reset();
        context.setAssetManager(nullptr);
        mPhysicsSystem.reset();
        context.setPhysicsSystem(nullptr);
        mRenderSystem.reset();
        context.setRenderTagRegistry(nullptr);
        context.setRenderFrontend(nullptr);
        context.setRendererContext(nullptr);
        context.setInputSystem(nullptr);
        context.setConfig(nullptr);

        // 尽量晚才停日志——保证上面这些清理过程里如果有任何 LOG_* 调用也能被记录下来。
        LogSystem::get().shutdown();
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

    void Engine::shutdown()
    {
        mScene = nullptr;
        mPhysicsSystem->shutdown();
        mRenderSystem->shutdown();
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

        renderFrontend.render(deltaTime);
        renderFrontend.endFrame();
    }

}
