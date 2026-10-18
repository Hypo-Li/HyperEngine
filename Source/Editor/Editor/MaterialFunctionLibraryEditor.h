#pragma once
#include "AssetEditorWindow.h"
#include "SlangCodeEditor.h"

#include <Core/Common/Guid.h>
#include <Engine/Material/MaterialFunctionLibrary.h>

#include <string>

namespace hyp::ed
{
    class ThumbnailProvider;
    class AssetBrowser;
    class UndoStack;

    class MaterialFunctionLibraryEditor final : public AssetEditorWindow
    {
    public:
        MaterialFunctionLibraryEditor(Asset& asset, UndoStack& undoStack, ThumbnailProvider& iconProvider, AssetBrowser& assetBrowser);

        // MaterialFunctionLibraryEditorTab::hasUnsavedChanges() 用: 关闭 tab 前要不要弹
        // 确认框——只看"还没 Apply"(源码文本还留在 SlangCodeEditor 缓冲区里), 不看
        // "Apply 过了但还没存盘", 见 EditorTab::hasUnsavedChanges() 的注释。
        bool needsApply() const noexcept;

        // MaterialFunctionLibraryEditorTab::applyPendingChanges() 用: 关闭确认框点了
        // "Apply"时调用。只把源码文本的写回(setLibrarySource())延迟到这里做, 库引用的
        // 增删(drawImportedLibraries())点击就立即生效, 不经过这层缓冲。
        void applyPendingChanges();

        // 目前只有 drawApplyAndSave() 的 Save 按钮读它, 跟"要不要弹关闭确认框"无关。
        bool needsSave() const noexcept;

    protected:
        void onDraw() override;

    private:
        void drawImportedLibraries();
        void drawSource();
        void drawApplyAndSave();

        // 不含 MaterialInput/MaterialOutput 声明, 引用这些类型的库函数会被编辑器误报未声明
        // 标识符(真正编译时没有问题)——库不绑定到具体 MaterialTemplate, 没有唯一确定的
        // ShadingModel 可用来生成这份声明
        std::string buildSourcePreamble() const;

        ThumbnailProvider& mIconProvider;
        AssetBrowser& mAssetBrowser;
        TRef<MaterialFunctionLibrary> mLibrary;

        SlangCodeEditor mSourceEditor;
        bool mSourceModified = false;

        Guid mNewImportedLibraryGuid;
        std::string mImportError;
    };
}
