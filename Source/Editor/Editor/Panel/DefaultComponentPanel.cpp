#include "DefaultComponentPanel.h"

#include <imgui.h>

namespace hyp::ed
{
    DefaultComponentPanel::DefaultComponentPanel(Component& component) :
        IComponentPanel(component)
    {
    }

    void DefaultComponentPanel::onDraw()
    {
        ImGui::TextDisabled("No custom inspector available.");
    }
}
