#include "MaterialTemplateEditorTab.h"
#include "MaterialTemplateEditor.h"

#include <imgui_internal.h>

namespace hyp::ed
{
    MaterialTemplateEditorTab::MaterialTemplateEditorTab(Asset& asset, ThumbnailProvider& iconProvider, AssetBrowser& assetBrowser) :
        AssetEditorTab(asset),
        mEditor(std::make_unique<MaterialTemplateEditor>(asset, sharedUndoStack(), iconProvider, assetBrowser))
    {
    }

    MaterialTemplateEditorTab::~MaterialTemplateEditorTab() = default;

    bool MaterialTemplateEditorTab::hasUnsavedChanges() const
    {
        return mEditor->needsApply();
    }

    void MaterialTemplateEditorTab::applyPendingChanges()
    {
        mEditor->applyPendingChanges();
    }

    void MaterialTemplateEditorTab::buildDefaultLayout(ImGuiID dockspaceId)
    {
        ImGui::DockBuilderDockWindow(mEditor->getLabel().c_str(), dockspaceId);
    }

    void MaterialTemplateEditorTab::drawWindows()
    {
        mEditor->draw();
    }
}
