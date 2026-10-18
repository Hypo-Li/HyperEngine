#pragma once
#include <Core/Common/HString.h>

#include <imgui.h>

#include <any>
#include <deque>
#include <functional>
#include <unordered_map>

namespace hyp::ed
{
    // 编辑器全局的线性 Undo/Redo 栈: push() 一条新命令会清空 redo 链(标准语义——一旦有新操作,
    // 原来能重做的分支就失效了)。栈深度有上限, 超过后丢最旧的一条, 避免长时间编辑内存无限涨。
    //
    // 场景加载/切换功能做出来之后, 切场景时必须调 clear()——这里的 undo/redo lambda 会捕获
    // 具体的 Transform&/Entity* 等引用, 场景一换这些引用就悬空了。现在还没有场景加载路径,
    // 不会真的触发, 但这是这个类往后一定要遵守的使用约定。
    class UndoStack
    {
    public:
        void push(HString name, std::function<void()> undo, std::function<void()> redo);

        void undo();
        void redo();

        bool canUndo() const noexcept
        {
            return !mUndoStack.empty();
        }

        bool canRedo() const noexcept
        {
            return !mRedoStack.empty();
        }

        HString peekUndoName() const noexcept;
        HString peekRedoName() const noexcept;

        void clear() noexcept;

        // 紧跟在 Drag/Slider/InputText 等控件调用之后调用(中间不能插入别的 item——内部
        // ImGui::GetItemID()/IsItemActivated()/IsItemDeactivatedAfterEdit() 查的都是"上一个
        // 绘制的 item"的状态)。beforeWidgetValue 必须是控件调用**之前**从 model 读出来的快照,
        // currentValue 是控件调用之后的值。
        //
        // ImGui 自己保证了同一个控件的 ImGuiID 跨帧稳定, 借用它当 key 存"手势开始时的快照",
        // 不需要调用方(TransformPanel 这类面板本身每帧都是新建的临时对象)自己维护跨帧状态。
        template <typename T, typename Setter>
        void trackGesture(HString name, const T& beforeWidgetValue, const T& currentValue, Setter setter)
        {
            ImGuiID id = ImGui::GetItemID();
            if (ImGui::IsItemActivated())
                mGestureSnapshots[id] = beforeWidgetValue;

            if (ImGui::IsItemDeactivatedAfterEdit())
            {
                auto it = mGestureSnapshots.find(id);
                if (it != mGestureSnapshots.end())
                {
                    T before = std::any_cast<T>(it->second);
                    mGestureSnapshots.erase(it);

                    // IsItemDeactivatedAfterEdit() 对 Combo()/ListBox()/Selectable() 点击已选中的
                    // 项也会误报(ImGui 文档自己写的), 这里用 before == currentValue 过滤掉,
                    // 不产生空的 undo 记录
                    if (!(before == currentValue))
                    {
                        push(name,
                            [setter, before]() mutable { setter(before); },
                            [setter, currentValue]() mutable { setter(currentValue); });
                    }
                }
            }
        }

    private:
        struct Command
        {
            HString name;
            std::function<void()> undo;
            std::function<void()> redo;
        };

        static constexpr size_t kMaxDepth = 128;

        // 超过 kMaxDepth 之后要从最旧的一端(front)丢掉——std::vector::erase(begin()) 是
        // O(n)(要把后面所有元素整体搬一位), 用 std::deque 换成 O(1) 的 pop_front()。
        std::deque<Command> mUndoStack;
        std::deque<Command> mRedoStack;
        std::unordered_map<ImGuiID, std::any> mGestureSnapshots;
    };
}
