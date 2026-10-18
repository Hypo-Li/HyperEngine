#pragma once
#include "EditorWindow.h"

namespace hyp
{
    class Asset;
}

namespace hyp::ed
{
    class UndoStack;

    // 编辑单个资产的窗口的公共基类——用资产的 Guid 当 ImGui 窗口 ID、资产名字当标题, 这样
    // 标题重名也不会冲突。UndoStack 由所属 AssetEditorTab 构造时直接传引用进来——同一个
    // tab 以后如果拆出多个子面板, 共用的是同一份历史, 不是各自独立的, 见 AssetEditorTab
    // 的注释。
    //
    // 派生类通过 AssetEditorRegistrar<AssetT, TabT> 自注册到 AssetEditorRegistry, 由
    // AssetBrowser 双击资产时统一负责打开/去重, 不需要自己关心这一层。
    class AssetEditorWindow : public EditorWindow
    {
    public:
        AssetEditorWindow(Asset& asset, UndoStack& undoStack, ImGuiWindowFlags flags = ImGuiWindowFlags_None);

        // 标题栏尾巴加/去掉一个脏标记星号, 只在 mAsset.isDirty() 翻转时调 setTitle()
        void draw() override;

    protected:
        Asset& getAsset() const noexcept
        {
            return mAsset;
        }

        UndoStack* getUndoStack() noexcept override
        {
            return &mUndoStack;
        }

        // 派生类内部调用手势追踪/push 的时候用这个引用形式, 比每次解引用
        // getUndoStack() 方便——getUndoStack() 本身是为了满足 EditorWindow 的虚函数签名
        // (返回指针、可能为 nullptr)才存在的。
        UndoStack& undoStack() noexcept
        {
            return mUndoStack;
        }

    private:
        Asset& mAsset;
        UndoStack& mUndoStack;
        bool mLastKnownDirty = false;
    };
}
