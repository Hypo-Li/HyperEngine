#pragma once
#include "EditorWindow.h"
#include "EditorSelection.h"
#include "UndoStack.h"
#include "Panel/ComponentPanel.h"
#include "Panel/TransformPanel.h"

#include <memory>
#include <vector>

namespace hyp
{
    class Entity;
}

namespace hyp::ed
{
    class ThumbnailProvider;
    class AssetBrowser;

    class Inspector : public EditorWindow
    {
    public:
        Inspector(EditorSelection& selection, UndoStack& undoStack, ThumbnailProvider& iconProvider, AssetBrowser& assetBrowser);

        void onDraw() override;

        // 焦点路由用: Inspector 聚焦时, 全局 Ctrl+Z/Ctrl+Y 操作的是 Editor 持有的这份共享
        // UndoStack(场景/Component 编辑用的那个), 见 EditorWindow::getUndoStack() 的注释。
        UndoStack* getUndoStack() noexcept override
        {
            return &mUndoStack;
        }

        TransformPanel* getTransformPanel() const noexcept
        {
            return mTransformPanel.get();
        }

    private:
        // entity 当前的 Component 列表跟这份缓存对不上(数量、指针、顺序任一不同)就整体
        // 重建——切换选中 Entity、Add Component、Remove Component 都会导致列表变化, 用
        // 同一个比对覆盖这三种情况, 不需要分别设标志位处理
        void refreshComponentPanels(Entity& entity);

        // 删除前把 component 自己的属性序列化一份快照, undo 时用快照重建出一个新对象重新
        // 挂回 Entity; redo 按 Class + Guid 现查一遍再删, 不能对着 undo 恢复出来的新地址
        // 对象留一份旧指针。具体的序列化/反序列化机制见 Inspector.cpp 里的
        // captureComponentSnapshot()/attachComponentFromSnapshot()/detachComponentByRef()。
        void removeComponentWithUndo(Entity& entity, Component* component);

        // 跟 removeComponentWithUndo() 互为镜像: 新增先真正执行(entity.addComponent()),
        // 再序列化一份刚加完的快照——undo = 按 ref 删掉, redo = 用快照重新挂回去, 用的是
        // 同一套 captureComponentSnapshot()/attachComponentFromSnapshot()/
        // detachComponentByRef()。快照捕获 Guid, 意味着 undo 之后再 redo 出来的还是
        // 同一个 Guid 的对象, 不会因为重新调用 addComponent() 铸出一个新 Guid。
        void addComponentWithUndo(Entity& entity, Class* componentClass);

        // 把 dragged 移到 target 之前或之后, 位置没变时不产生 undo 记录。
        void moveComponentWithUndo(Component& dragged, Component& target, bool insertAfter);

    private:
        EditorSelection& mSelection;
        UndoStack& mUndoStack;
        ThumbnailProvider& mIconProvider;
        AssetBrowser& mAssetBrowser;
        TransformSpace mTransformSpace = TransformSpace::World;
        bool mScaleLocked = true;
        std::unique_ptr<TransformPanel> mTransformPanel;
        std::vector<std::unique_ptr<IComponentPanel>> mComponentPanels;
        Class* mSelectedAddComponentClass = nullptr; // 下拉框只是选类型, 按下 Add 按钮才真正添加
    };
}
