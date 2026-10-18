#include "EditorTab.h"

namespace hyp::ed
{
    EditorTab::EditorTab(std::string tabId) :
        mTabId(std::move(tabId))
    {
    }

    void EditorTab::draw(float topReserved)
    {
        topReserved += drawExtraChrome(topReserved);
        mDockSpace.draw(topReserved, mTabId, [this](ImGuiID dockspaceId) { buildDefaultLayout(dockspaceId); });
        drawWindows();
    }
}
