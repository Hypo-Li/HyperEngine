#include "TextureEditorTab.h"
#include "TextureEditor.h"

#include <imgui_internal.h>

namespace hyp::ed
{
    TextureEditorTab::TextureEditorTab(Asset& asset, ThumbnailProvider& iconProvider, AssetBrowser& assetBrowser) :
        AssetEditorTab(asset),
        mEditor(std::make_unique<TextureEditor>(asset, sharedUndoStack(), iconProvider, assetBrowser))
    {
    }

    TextureEditorTab::~TextureEditorTab() = default;

    void TextureEditorTab::buildDefaultLayout(ImGuiID dockspaceId)
    {
        ImGui::DockBuilderDockWindow(mEditor->getLabel().c_str(), dockspaceId);
    }

    void TextureEditorTab::drawWindows()
    {
        mEditor->draw();
    }

    void TextureEditorTab::collectPrepareTextures(std::vector<TextureProxyHandle>& out)
    {
        mEditor->collectPrepareTextures(out);
    }
}
