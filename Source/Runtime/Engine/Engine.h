#pragma once
#include "Config.h"
#include "Context.h"
#include "Framework/Scene.h"
#include "Input/InputSystem.h"
#include "Physics/PhysicsSystem.h"
#include "Rendering/RenderSystem.h"

#include <Core/Asset/AssetManager.h>
#include <Platform/Window.h>

namespace hyp
{
    class Engine
    {
    public:
        explicit Engine(Context::Environment environment = Context::Environment::Standalone);

        ~Engine();

        template <typename T>
        T& get() noexcept;

        void setScene(Scene* scene)
        {
            mScene = scene;
        }

        Scene* getScene() const noexcept
        {
            return mScene.get();
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

        void shutdown();

    private:
        void tick(float deltaTime);

    private:
        std::unique_ptr<Config> mConfig;
        std::unique_ptr<Window> mWindow;
        std::unique_ptr<InputSystem> mInputSystem;
        std::unique_ptr<RenderSystem> mRenderSystem;
        std::unique_ptr<AssetManager> mAssetManager;
        std::unique_ptr<PhysicsSystem> mPhysicsSystem;
        TRef<Scene> mScene;

        bool mRunning = true;

        std::function<void(float)> onPreTick;
        std::function<void()> onPreRender;
    };

    // 显式特化的成员模板必须写在命名空间作用域, 不能写在类体内(MSVC 作为扩展允许类体内写法,
    // GCC/Clang 按标准拒绝), 所以挪到类定义外面, 用 inline 避免多个 TU 包含这个头文件时的重定义。
    template <> inline Window& Engine::get() noexcept { return *mWindow; }
    template <> inline InputSystem& Engine::get() noexcept { return *mInputSystem; }
    template <> inline RenderSystem& Engine::get() noexcept { return *mRenderSystem; }
    template <> inline AssetManager& Engine::get() noexcept { return *mAssetManager; }
    template <> inline PhysicsSystem& Engine::get() noexcept { return *mPhysicsSystem; }
    template <> inline Config& Engine::get() noexcept { return *mConfig; }
}
