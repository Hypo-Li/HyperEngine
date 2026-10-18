#pragma once

namespace hyp::ed
{
    class EditorTabManager;

    // 只画 File/Window 这些菜单项, 不自己开 BeginMainMenuBar/EndMainMenuBar——跟 EditorTabBar
    // 共享同一行, 由 Editor::run() 统一包一层。
    class EditorMenuBar
    {
    public:
        explicit EditorMenuBar(EditorTabManager& tabManager);

        void drawMenus();

    private:
        void drawFileMenu();
        void drawWindowMenu();

        EditorTabManager& mTabManager;
    };
}
