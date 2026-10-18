#pragma once
#include <imgui.h>

#include <functional>
#include <vector>

namespace hyp::ed
{
    class EditorTabManager;

    // 全局快捷键统一登记的地方——每条绑定是"按键组合 + 动作"这样一行, 集中列在一处,
    // 不散落进各个类里; 以后新增全局快捷键只需要往构造函数里的表里加一行。只覆盖"不需要
    // 哪个窗口有焦点就该生效"的这一类(Undo/Redo、标签页切换)。需要特定窗口有焦点才生效的
    // 工具型快捷键(比如以后 SceneView 的 G/S/R 模态变换)不归这里管——那类复杂度全在模态
    // 状态机本身, 各自长在对应窗口里, 硬塞进这张表没有好处。
    class GlobalShortcuts
    {
    public:
        explicit GlobalShortcuts(EditorTabManager& tabManager);

        // 每帧调用一次, 逐条检查表里的绑定
        void update();

    private:
        struct Binding
        {
            ImGuiKey key;
            bool ctrl;
            bool shift;
            bool alt;
            // 正在打字的文本框是否应该挡住这条——Undo/Redo 要挡(文本框自己有一套 Ctrl+Z
            // 撤销输入字符的逻辑, 靠 stb_textedit, 跟"编辑器操作"级别的撤销不是一回事);
            // 切标签页这类不涉及文本内容的不用挡。
            bool blockWhileTextInput;
            std::function<void()> execute;
        };

        EditorTabManager& mTabManager;
        std::vector<Binding> mBindings;
    };
}
