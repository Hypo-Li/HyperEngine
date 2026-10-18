#pragma once

#include <Core/Common/Guid.h>
#include <Core/Object/Object.h>
#include <Core/Reflection/Class.h>

#include <optional>

namespace hyp::ed
{
    enum class InspectorPanelAction
    {
        None,
        Remove,
        Reorder,
    };

    // 被拖动的面板目标 Guid, 以及这次放下是插到本面板之前还是之后。
    struct PanelReorderRequest
    {
        Guid draggedGuid;
        bool insertAfter;
    };

    class IInspectorPanel
    {
    public:
        explicit IInspectorPanel(Object& target) noexcept;
        virtual ~IInspectorPanel() = default;

        InspectorPanelAction draw();

        // 默认关闭, 只有需要拖动调整面板顺序的窗口(Inspector)才打开。
        void setReorderEnabled(bool enabled) noexcept
        {
            mReorderEnabled = enabled;
        }

        // draw() 返回 Reorder 之后取走这次的请求。
        PanelReorderRequest takeReorderRequest() noexcept
        {
            PanelReorderRequest request = *mPendingReorder;
            mPendingReorder.reset();
            return request;
        }

    protected:
        // 默认按 mTarget 的反射类显示名——多数面板(组件/Transform)标题就是这个, 不需要各自
        // 重写; 真正需要跟类名不一样的标题(目前没有)才重写这个
        virtual const char* getTitle() const noexcept
        {
            return mTarget.getClass()->getDisplayName().toCString();
        }

        virtual bool canRemove() const noexcept;
        virtual void onDraw() = 0;

        Object& getTarget() const noexcept;

    private:
        Object& mTarget;
        bool mReorderEnabled = false;
        std::optional<PanelReorderRequest> mPendingReorder;
    };
}
