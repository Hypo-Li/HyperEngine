#pragma once
#include <RenderBase/RenderProxy.h>

namespace hyp
{
    class SceneProxy;

    class SceneElementProxy : public RenderProxy
    {
    public:
        SceneElementProxy(SceneProxy& sceneProxy, bool visible) noexcept :
            mVisible(visible),
            mSceneProxy(sceneProxy)
        {
        }

        void setVisible(bool visible) noexcept { mVisible = visible; }

        bool isVisible() const noexcept { return mVisible; }

        SceneProxy& getSceneProxy() const noexcept
        {
            return mSceneProxy;
        }

    private:
        bool mVisible;
        SceneProxy& mSceneProxy;
    };
}
