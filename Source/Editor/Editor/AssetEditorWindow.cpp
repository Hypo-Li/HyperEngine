#include "AssetEditorWindow.h"

#include <Core/Asset/Asset.h>

namespace hyp::ed
{
    AssetEditorWindow::AssetEditorWindow(Asset& asset, UndoStack& undoStack, ImGuiWindowFlags flags) :
        EditorWindow(std::string(asset.getName().toStringView()), asset.getRootGuid().toString(), flags),
        mAsset(asset),
        mUndoStack(undoStack)
    {
    }

    void AssetEditorWindow::draw()
    {
        bool dirty = mAsset.isDirty();
        if (dirty != mLastKnownDirty)
        {
            mLastKnownDirty = dirty;
            setTitle(std::string(mAsset.getName().toStringView()) + (dirty ? " *" : ""));
        }
        EditorWindow::draw();
    }
}
