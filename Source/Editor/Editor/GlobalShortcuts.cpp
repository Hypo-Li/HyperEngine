#include "GlobalShortcuts.h"
#include "EditorTabManager.h"
#include "UndoStack.h"

namespace hyp::ed
{
    GlobalShortcuts::GlobalShortcuts(EditorTabManager& tabManager) :
        mTabManager(tabManager)
    {
        mBindings = {
            { ImGuiKey_Z, true, false, false, true, [this]
                {
                    if (UndoStack* stack = mTabManager.getFocusedUndoStack())
                        stack->undo();
                }
            },
            { ImGuiKey_Z, true, true, false, true, [this]
                {
                    if (UndoStack* stack = mTabManager.getFocusedUndoStack())
                        stack->redo();
                }
            },
            { ImGuiKey_Q, true, false, false, false, [this] { mTabManager.activatePreviousTab(); } },
            { ImGuiKey_E, true, false, false, false, [this] { mTabManager.activateNextTab(); } },
        };
    }

    void GlobalShortcuts::update()
    {
        ImGuiIO& io = ImGui::GetIO();

        for (const Binding& binding : mBindings)
        {
            if (binding.blockWhileTextInput && io.WantTextInput)
                continue;
            if (binding.ctrl != io.KeyCtrl || binding.shift != io.KeyShift || binding.alt != io.KeyAlt)
                continue;
            if (ImGui::IsKeyPressed(binding.key, false))
                binding.execute();
        }
    }
}
