#pragma once
#include <string>
#include <vector>
#include <functional>
#include <atomic>

#include <Core/Common/Refable.h>
#include <Core/Math/Vector.h>

namespace hyp
{
    enum class WindowingSystem
    {
        Unknown,
        Win32,
        X11,
        Wayland,
    };

    class Window : public Refable
    {
    public:
        Window(int width, int height, const std::string& title);
        ~Window();

        // 仅在 Linux 上有意义,返回当前 GLFW 实际选中的 windowing system
        static WindowingSystem getWindowingSystem();

        static void pollEvents();

        static void waitEvents();

        void setFramebufferSize(uint2 size);

        uint2 getFramebufferSize() const;

        double2 getCursorPosition() const;

        double2 getScrollOffset() const;

        float2 getContentScale() const;

        void captureCursor() const;

        void releaseCursor() const;

        bool shouldClose() const;

        void setResizeCallback(std::function<void(Window*, int, int)>&& callback);

        void setMouseButtonCallback(std::function<void(Window*, int, int, int)>&& callback);

        void setCursorPositionCallback(std::function<void(Window*, double, double)>&& callback);

        void setScrollCallback(std::function<void(Window*, double, double)>&& callback);

        void setKeyCallback(std::function<void(Window*, int, int, int, int)>&& callback);

        static void setJoystickCallback(std::function<void(int, int)>&& callback);

        void getGlfwHandle(void* glfwWindow) const;

        void getWin32Handle(void* hWnd) const;

        void getX11Display(void* display) const;

        void getX11Handle(void* window) const;

        void getWaylandDisplay(void* display) const;

        void getWaylandSurface(void* surface) const;

    private:
        std::atomic<uint2> mFramebufferSize;
        std::string mTitle;
        void* mWindow;
    };
}
