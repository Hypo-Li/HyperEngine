#include "SceneView.h"

#include <Engine/Framework/Component.h>
#include <Engine/Framework/Entity.h>

#include <HypImGui/IconsMaterialSymbols.h>

#include <imgui.h>

namespace hyp::ed
{
    SceneView::SceneView(Engine& engine, EditorSelection& selection, UndoStack& undoStack) :
        EditorWindow("SceneView", "SceneView"),
        mEngine(engine),
        mSelection(selection),
        mUndoStack(undoStack),
        mViewport("SceneView.Viewport0")
    {
        setIcon(ICON_MS_VIEW_IN_AR);
        setWindowPadding(ImVec2(0.0f, 0.0f));

        // 点中什么、要不要选中完全是这个窗口自己的事——SceneViewport 本身不知道 EditorSelection
        // 存在, 见 PickResult 的注释
        mViewport.setPickCallback(
            [this](PickResult result)
            {
                if (result.component)
                    mSelection.selectOnly(result.component->getEntity()->getGuid());
                else
                    mSelection.clear();
            }
        );
    }

    SceneView::~SceneView()
    {
    }

    void SceneView::onDraw()
    {
        ImVec2 availSize = ImGui::GetContentRegionAvail();
        if (availSize.x <= 0.0f || availSize.y <= 0.0f)
            return;

        mViewport.draw(mEngine.getScene(), availSize, ImGui::GetIO().DeltaTime);
    }
}
