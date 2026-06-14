#pragma once

namespace hyp
{
    class Engine;
    class InputSystem;
    class SampleStateManager;
    class RenderFrontend;
    class SceneRendererContext;

    class Context
    {
        friend class Engine;
    public:
        static Context& get();

        Context(const Context&) = delete;
        Context& operator=(const Context&) = delete;
        ~Context() = default;

        InputSystem& getInputSystem() const noexcept
        {
            return *mInputSystem;
        }

        SampleStateManager& getSampleStateManager() const noexcept
        {
            return *mSampleStateManager;
        }

        RenderFrontend& getRenderFrontend() const noexcept
        {
            return *mRenderFrontend;
        }

        SceneRendererContext& getSceneRendererContext() const noexcept
        {
            return *mSceneRendererContext;
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

        void setSceneRendererContext(SceneRendererContext* sceneRendererContext)
        {
            mSceneRendererContext = sceneRendererContext;
        }

    private:
        InputSystem* mInputSystem;
        SampleStateManager* mSampleStateManager;
        RenderFrontend* mRenderFrontend;
        SceneRendererContext* mSceneRendererContext;
        Environment mEnvironment;
    };
}
