#include "MaterialInstanceEditorTab.h"
#include "MaterialInstanceEditor.h"

#include <imgui_internal.h>

namespace hyp::ed
{
    MaterialInstanceEditorTab::MaterialInstanceEditorTab(Asset& asset, ThumbnailProvider& iconProvider, AssetBrowser& assetBrowser) :
        AssetEditorTab(asset),
        mEditor(std::make_unique<MaterialInstanceEditor>(asset, sharedUndoStack(), iconProvider, assetBrowser))
    {
    }

    MaterialInstanceEditorTab::~MaterialInstanceEditorTab() = default;

    void MaterialInstanceEditorTab::buildDefaultLayout(ImGuiID dockspaceId)
    {
        ImGui::DockBuilderDockWindow(mEditor->getLabel().c_str(), dockspaceId);
    }

    void MaterialInstanceEditorTab::drawWindows()
    {
        mEditor->draw();
    }
}
