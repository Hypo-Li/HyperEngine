#include "FontEditorTab.h"
#include "FontEditor.h"

#include <imgui_internal.h>

namespace hyp::ed
{
    FontEditorTab::FontEditorTab(Asset& asset, ThumbnailProvider& iconProvider, AssetBrowser& assetBrowser) :
        AssetEditorTab(asset),
        mEditor(std::make_unique<FontEditor>(asset, sharedUndoStack(), iconProvider, assetBrowser))
    {
    }

    FontEditorTab::~FontEditorTab() = default;

    void FontEditorTab::buildDefaultLayout(ImGuiID dockspaceId)
    {
        ImGui::DockBuilderDockWindow(mEditor->getLabel().c_str(), dockspaceId);
    }

    void FontEditorTab::drawWindows()
    {
        mEditor->draw();
    }

    void FontEditorTab::collectPrepareTextures(std::vector<TextureProxyHandle>& out)
    {
        mEditor->collectPrepareTextures(out);
    }
}
