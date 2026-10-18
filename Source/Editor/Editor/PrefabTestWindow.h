#pragma once
#include "EditorWindow.h"
#include "EditorSelection.h"
#include "UndoStack.h"
#include "Panel/ComponentPanel.h"

#include <Engine/Engine.h>
#include <Engine/Framework/Prefab.h>

#include <memory>
#include <vector>

namespace hyp::ed
{
    class ThumbnailProvider;
    class AssetBrowser;

    // 临时调试窗口: Prefab 目前完全没有编辑器 UI(创建/实例化/编辑模板/应用全靠 Editor.cpp
    // 构造函数里手写的测试代码), 也还不是真正的磁盘 Asset, 没有别的地方能摸到模板对象——
    // 这个窗口给这条链路搭一个能点的最小入口, 用来实测 Prefab 同步逻辑(尤其是 Transform
    // 同步), 不是 Prefab_System_Design.md §7.5 说的那个"正式编辑器 UI"。
    class PrefabTestWindow : public EditorWindow
    {
    public:
        PrefabTestWindow(Engine& engine, EditorSelection& selection, ThumbnailProvider& iconProvider, AssetBrowser& assetBrowser);

        // 跟场景的 Ctrl+Z/Ctrl+Y 各自独立(AssetEditorWindow 那套"每个编辑窗口自己攥一份
        // UndoStack"的约定), 不共用 Hierarchy/Inspector 那份共享栈——模板编辑跟场景编辑是
        // 两回事, 混在一起撤销会让人搞不清这一步到底改的是哪边。
        UndoStack* getUndoStack() noexcept override
        {
            return &mUndoStack;
        }

    protected:
        void onDraw() override;

    private:
        void drawTemplateEntityList();
        void drawSelectedEntityEditor();

        // mSelectedTemplateEntity 的 Component 列表跟这份缓存对不上就整体重建——道理跟
        // Inspector::refreshComponentPanels() 一样。
        void refreshComponentPanels();

    private:
        Engine& mEngine;
        EditorSelection& mSelection;
        ThumbnailProvider& mIconProvider;
        AssetBrowser& mAssetBrowser;
        UndoStack mUndoStack;

        TRef<Prefab> mTestPrefab;
        Entity* mSelectedTemplateEntity = nullptr;
        std::vector<std::unique_ptr<IComponentPanel>> mComponentPanels;
    };
}
