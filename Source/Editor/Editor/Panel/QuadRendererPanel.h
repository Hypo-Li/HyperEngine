#pragma once

#include "ComponentPanel.h"
#include "../UndoStack.h"

#include <Engine/Component/Renderer/QuadRenderer.h>

#include <HypImGui/ImGuiPropertyWidgets.h>

#include <string>

namespace hyp::ed
{
    // QuadRenderer 派生类面板共用的部分; 派生类自己的 onDraw() 负责建属性表, 在表里先调
    // drawCommonRows() 再画自己的行
    class QuadRendererPanel : public IComponentPanel
    {
    public:
        explicit QuadRendererPanel(QuadRenderer& quadRenderer) noexcept;

    protected:
        void drawCommonRows();

        // widget 画控件并返回是否改动, 参数是控件直接读写的当前值; setter 同时用于实时应用、
        // 撤销、重做和重置
        template <typename T, typename WidgetFn, typename Setter>
        void drawProperty(const char* label, T current, const T& defaultValue, WidgetFn widget, Setter setter, float rowMultiplier = 1.0f)
        {
            BeginPropertyRow(label, rowMultiplier);
            T before = current;
            T value = current;
            bool changed = widget(value);
            getUndoStack().trackGesture(HString(std::string("Change ") + label), before, value, setter);
            if (changed)
                setter(value);

            T beforeReset = value;
            if (DrawPropertyReset(label, value, defaultValue))
            {
                setter(value);
                getUndoStack().push(HString(std::string("Reset ") + label),
                    [setter, beforeReset]() mutable { setter(beforeReset); },
                    [setter, value]() mutable { setter(value); });
            }
        }

    private:
        QuadRenderer& mQuadRenderer;
    };
}
