#include "MaterialFunctionLibraryEditorTab.h"
#include "MaterialFunctionLibraryEditor.h"

#include <imgui_internal.h>

namespace hyp::ed
{
    MaterialFunctionLibraryEditorTab::MaterialFunctionLibraryEditorTab(Asset& asset, ThumbnailProvider& iconProvider, AssetBrowser& assetBrowser) :
        AssetEditorTab(asset),
        mEditor(std::make_unique<MaterialFunctionLibraryEditor>(asset, sharedUndoStack(), iconProvider, assetBrowser))
    {
    }

    MaterialFunctionLibraryEditorTab::~MaterialFunctionLibraryEditorTab() = default;

    bool MaterialFunctionLibraryEditorTab::hasUnsavedChanges() const
    {
        return mEditor->needsApply();
    }

    void MaterialFunctionLibraryEditorTab::applyPendingChanges()
    {
        mEditor->applyPendingChanges();
    }

    void MaterialFunctionLibraryEditorTab::buildDefaultLayout(ImGuiID dockspaceId)
    {
        ImGui::DockBuilderDockWindow(mEditor->getLabel().c_str(), dockspaceId);
    }

    void MaterialFunctionLibraryEditorTab::drawWindows()
    {
        mEditor->draw();
    }
}
