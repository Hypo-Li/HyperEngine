#include "StaticMeshEditorTab.h"
#include "StaticMeshEditor.h"

#include <imgui_internal.h>

namespace hyp::ed
{
    StaticMeshEditorTab::StaticMeshEditorTab(Asset& asset, ThumbnailProvider& iconProvider, AssetBrowser& assetBrowser) :
        AssetEditorTab(asset),
        mEditor(std::make_unique<StaticMeshEditor>(asset, sharedUndoStack(), iconProvider, assetBrowser))
    {
    }

    StaticMeshEditorTab::~StaticMeshEditorTab() = default;

    void StaticMeshEditorTab::buildDefaultLayout(ImGuiID dockspaceId)
    {
        ImGui::DockBuilderDockWindow(mEditor->getLabel().c_str(), dockspaceId);
    }

    void StaticMeshEditorTab::drawWindows()
    {
        // 请求关闭的这一帧 tab 紧接着就会被销毁, 此时再 tick 预览场景会让已入队的渲染 pass 引用到被销毁的 SceneProxy
        if (isCloseRequested())
            return;

        mEditor->draw();
    }
}
