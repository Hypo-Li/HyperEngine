#pragma once

#include "InspectorPanel.h"

#include <Engine/Framework/Component.h>

#include <cassert>

namespace hyp::ed
{
    class UndoStack;
    class ThumbnailProvider;
    class AssetBrowser;
    class ComponentPanelRegistry;

    // 具体子类(比如以后的 MeshRendererPanel)只需要写 PanelT(ComponentT&) 这一个构造函数——
    // UndoStack/ThumbnailProvider/AssetBrowser 不经过构造函数传入, 而是 ComponentPanelRegistry
    // 在 createPanel() 里构造完实例之后, 通过下面 private 的 injectDependencies() 直接注入。
    // 子类完全不知道、也没有渠道碰这个方法(它是 private 的, 只对 ComponentPanelRegistry 开放
    // friend 权限), 只需要在 onDraw() 里调 getUndoStack()/getIconProvider()/getAssetBrowser()
    // 就能用——不需要为这几个每个面板都用得上的依赖各自声明构造参数、层层转发。
    class IComponentPanel : public IInspectorPanel
    {
        friend class ComponentPanelRegistry;
    public:
        explicit IComponentPanel(Component& component) noexcept :
            IInspectorPanel(component),
            mComponent(component)
        {
        }

        Component& getComponent() const noexcept
        {
            return mComponent;
        }

    protected:
        bool canRemove() const noexcept override
        {
            return true;
        }

        UndoStack& getUndoStack() const noexcept
        {
            assert(mUndoStack && "UndoStack not injected yet - only valid after ComponentPanelRegistry::createPanel() returns");
            return *mUndoStack;
        }

        ThumbnailProvider& getIconProvider() const noexcept
        {
            assert(mIconProvider && "ThumbnailProvider not injected yet - only valid after ComponentPanelRegistry::createPanel() returns");
            return *mIconProvider;
        }

        AssetBrowser& getAssetBrowser() const noexcept
        {
            assert(mAssetBrowser && "AssetBrowser not injected yet - only valid after ComponentPanelRegistry::createPanel() returns");
            return *mAssetBrowser;
        }

    private:
        void injectDependencies(UndoStack& undoStack, ThumbnailProvider& iconProvider, AssetBrowser& assetBrowser) noexcept
        {
            mUndoStack = &undoStack;
            mIconProvider = &iconProvider;
            mAssetBrowser = &assetBrowser;
        }

    private:
        Component& mComponent;
        UndoStack* mUndoStack = nullptr;
        ThumbnailProvider* mIconProvider = nullptr;
        AssetBrowser* mAssetBrowser = nullptr;
    };
}
