#pragma once
#include <imgui.h>

#include <RenderBase/ProxyHandle.h>

#include <optional>
#include <string>
#include <vector>

namespace hyp::ed
{
    class UndoStack;

    class EditorWindow
    {
    public:
        virtual ~EditorWindow() = default;

        virtual void draw();

        // 上报需要每帧转成可采样布局才能被 ImGui::Image() 画出来的纹理(比如 TextureEditor
        // 的预览); 默认空实现, 大多数窗口(Hierarchy/Inspector 等不画任意纹理的)不需要覆写
        virtual void collectPrepareTextures(std::vector<TextureProxyHandle>& out) {}

        // 全局 Ctrl+Z/Ctrl+Y 快捷键按"当前哪个窗口有焦点"决定操作哪一个 UndoStack——默认没有
        // (nullptr), 意味着这类窗口不参与快捷键路由(Hierarchy/AssetBrowser 这些没有"连续
        // 编辑手势"概念的窗口就是这样)。Inspector 覆写它返回 Editor 持有的共享 UndoStack;
        // AssetEditorWindow(资产编辑器窗口的公共基类)覆写它返回所属 AssetEditorTab 的那份。
        virtual UndoStack* getUndoStack() noexcept
        {
            return nullptr;
        }

        void setTitle(std::string title);

        const std::string& getTitle() const noexcept
        {
            return mTitle;
        }

        // 传给 ImGui::Begin()/DockBuilderDockWindow() 的完整窗口名(标题 + "###"+id)——跟
        // getTitle() 不一样, 后者只是显示用的那半截, 会随 setTitle() 变(比如脏标记星号),
        // 这个不会变, ImGui 认的是这个。
        const std::string& getLabel() const noexcept
        {
            return mLabel;
        }

        bool isOpen() const noexcept
        {
            return mOpen;
        }

        void setOpen(bool open) noexcept
        {
            mOpen = open;
        }

        bool isFocused() const noexcept
        {
            return mFocused;
        }

        void requestFocus() noexcept
        {
            mFocusRequested = true;
        }

        bool isHovered() const noexcept
        {
            return mHovered;
        }

        // 是否在自己的标题栏画关闭按钮——只有 MainEditorTab 那几个常驻单例窗口需要(关闭 =
        // 隐藏, 对象还活着, 能从 Window 菜单勾选重新显示)。资产 tab 的内容窗口不需要:
        // 它们的生命周期完全等于所属 tab 的生命周期, 只能通过标签条自己的关闭按钮关掉, 见
        // AssetEditorTab。默认不显示。
        void setShowCloseButton(bool show) noexcept
        {
            mShowCloseButton = show;
        }

    protected:
        EditorWindow(std::string title, std::string id, ImGuiWindowFlags flags = ImGuiWindowFlags_None);

        virtual void onDraw() = 0;

        void setFlags(ImGuiWindowFlags flags)
        {
            mFlags = flags;
        }

        void setWindowPadding(ImVec2 padding) noexcept
        {
            mWindowPadding = padding;
        }

        // 图标只出现在窗口标题栏和停靠标签上, 不进 getTitle()。
        void setIcon(const char* icon);

    private:
        void rebuildLabel();

        std::string mTitle;
        std::string mIcon;
        std::string mId;
        std::string mLabel;
        bool mOpen = true;
        bool mFocused = false;
        bool mHovered = false;
        bool mShowCloseButton = false;
        ImGuiWindowFlags mFlags = ImGuiWindowFlags_None;
        std::optional<ImVec2> mWindowPadding;

        bool mFocusRequested = false;
    };
}
