#include "Window.h"
#include <Core/Common/LogSystem.h>

#include <GLFW/glfw3.h>
#ifdef WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#elif defined(__linux__)
// 实际用 X11 还是 Wayland 是运行时探测出来的(见构造函数),两边的 native API 都要暴露出来
#define GLFW_EXPOSE_NATIVE_X11
#define GLFW_EXPOSE_NATIVE_WAYLAND
#endif
#include <GLFW/glfw3native.h>

namespace hyp
{
    static std::unordered_map<void*, Window*> gWindowMap;

    static std::unordered_map<void*, std::function<void(Window*, int, int)>> gResizeCallbackMap;
    static void resizeCallback(GLFWwindow* window, int width, int height)
    {
        Window* wnd = gWindowMap.at(window);
        wnd->setFramebufferSize(uint2(width, height));
        auto it = gResizeCallbackMap.find(window);
        if (it != gResizeCallbackMap.end())
        {
            it->second(wnd, width, height);
        }
    }

    static std::unordered_map<void*, std::function<void(Window*, int, int, int)>> gMouseButtonCallbackMap;
    static void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods)
    {
        auto it = gMouseButtonCallbackMap.find(window);
        if (it != gMouseButtonCallbackMap.end())
        {
            it->second(gWindowMap.at(window), button, action, mods);
        }
    }

    static std::unordered_map<void*, std::function<void(Window*, double, double)>> gCursorPositionCallbackMap;
    static void cursorPositionCallback(GLFWwindow* window, double xpos, double ypos)
    {
        auto it = gCursorPositionCallbackMap.find(window);
        if (it != gCursorPositionCallbackMap.end())
        {
            it->second(gWindowMap.at(window), xpos, ypos);
        }
    }

    static std::unordered_map<void*, std::function<void(Window*, double, double)>> gScrollCallbackMap;
    static void scrollCallback(GLFWwindow* window, double xoffset, double yoffset)
    {
        auto it = gScrollCallbackMap.find(window);
        if (it != gScrollCallbackMap.end())
        {
            it->second(gWindowMap.at(window), xoffset, yoffset);
        }
    }

    static std::unordered_map<void*, std::function<void(Window*, int, int, int, int)>> gKeyCallbackMap;
    static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
    {
        auto it = gKeyCallbackMap.find(window);
        if (it != gKeyCallbackMap.end())
        {
            it->second(gWindowMap.at(window), key, scancode, action, mods);
        }
    }

    static std::function<void(int, int)> gJoystickCallback;
    static void joystickCallback(int jid, int event)
    {
        gJoystickCallback(jid, event);
    }

    static void windowIconifyCallback(GLFWwindow* window, int iconified)
    {
        uint2 size;
        if (iconified)
        {
            size = uint2(0, 0);
        }
        else
        {
            int width, height;
            glfwGetFramebufferSize(window, &width, &height);
            size = uint2(width, height);
        }
        Window* wnd = gWindowMap.at(window);
        wnd->setFramebufferSize(size);

        auto it = gResizeCallbackMap.find(window);
        if (it != gResizeCallbackMap.end())
        {
            it->second(wnd, size.x, size.y);
        }
    }

    Window::Window(int width, int height, const std::string& title) :
        mTitle(title)
    {
        if (gWindowMap.size() == 0)
        {
            glfwInit();
            glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
#ifdef _WIN32
            glfwWindowHint(GLFW_PLATFORM, GLFW_PLATFORM_WIN32);
#elif defined(__linux__)
            if (glfwPlatformSupported(GLFW_PLATFORM_WAYLAND))
                glfwWindowHint(GLFW_PLATFORM, GLFW_PLATFORM_WAYLAND);
            else
                glfwWindowHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
#endif
            glfwSetJoystickCallback(joystickCallback);
        }

        mWindow = glfwCreateWindow(width, height, mTitle.c_str(), nullptr, nullptr);
        mFramebufferSize.store(uint2{ width, height }, std::memory_order_release);
        glfwSetWindowSizeCallback((GLFWwindow*)mWindow, resizeCallback);
        glfwSetMouseButtonCallback((GLFWwindow*)mWindow, mouseButtonCallback);
        glfwSetCursorPosCallback((GLFWwindow*)mWindow, cursorPositionCallback);
        glfwSetScrollCallback((GLFWwindow*)mWindow, scrollCallback);
        glfwSetKeyCallback((GLFWwindow*)mWindow, keyCallback);
        glfwSetWindowIconifyCallback((GLFWwindow*)mWindow, windowIconifyCallback);
        gWindowMap.emplace(mWindow, this);
    }

    Window::~Window()
    {
        gWindowMap.erase(mWindow);
        glfwDestroyWindow((GLFWwindow*)mWindow);

        if (gWindowMap.size() == 0)
        {
            glfwTerminate();
        }
    }

    WindowingSystem Window::getWindowingSystem()
    {
#ifdef _WIN32
        return WindowingSystem::Win32;
#elif defined(__linux__)
        return glfwGetPlatform() == GLFW_PLATFORM_WAYLAND ? WindowingSystem::Wayland : WindowingSystem::X11;
#else
        return WindowingSystem::Unknow;
#endif
    }

    void Window::pollEvents()
    {
        glfwPollEvents();
    }

    void Window::waitEvents()
    {
        glfwWaitEvents();
    }

    void Window::setFramebufferSize(uint2 size)
    {
        mFramebufferSize.store(size, std::memory_order_release);
    }

    uint2 Window::getFramebufferSize() const
    {
        return mFramebufferSize.load(std::memory_order_acquire);
    }

    double2 Window::getCursorPosition() const
    {
        double xpos, ypos;
        glfwGetCursorPos((GLFWwindow*)mWindow, &xpos, &ypos);
        return double2{ xpos, ypos };
    }

    float2 Window::getContentScale() const
    {
        float xscale, yscale;
        glfwGetWindowContentScale((GLFWwindow*)mWindow, &xscale, &yscale);
        return float2{ xscale, yscale };
    }

    void Window::captureCursor() const
    {
        glfwSetInputMode((GLFWwindow*)mWindow, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    }

    void Window::releaseCursor() const
    {
        glfwSetInputMode((GLFWwindow*)mWindow, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    }

    bool Window::shouldClose() const
    {
        return glfwWindowShouldClose((GLFWwindow*)mWindow);
    }

    void Window::setResizeCallback(std::function<void(Window*, int, int)>&& callback)
    {
        gResizeCallbackMap[mWindow] = std::move(callback);
    }

    void Window::setMouseButtonCallback(std::function<void(Window*, int, int, int)>&& callback)
    {
        gMouseButtonCallbackMap[mWindow] = std::move(callback);
    }

    void Window::setCursorPositionCallback(std::function<void(Window*, double, double)>&& callback)
    {
        gCursorPositionCallbackMap[mWindow] = std::move(callback);
    }

    void Window::setScrollCallback(std::function<void(Window*, double, double)>&& callback)
    {
        gScrollCallbackMap[mWindow] = std::move(callback);
    }

    void Window::setKeyCallback(std::function<void(Window*, int, int, int, int)>&& callback)
    {
        gKeyCallbackMap[mWindow] = std::move(callback);
    }

    void Window::setJoystickCallback(std::function<void(int, int)>&& callback)
    {
        assert(!gWindowMap.empty() && "Create window first!");
        gJoystickCallback = std::move(callback);
    }

    void Window::getGlfwHandle(void* glfwWindow) const
    {
        *(GLFWwindow**)(glfwWindow) = (GLFWwindow*)(mWindow);
    }

    void Window::getWin32Handle(void* hWnd) const
    {
#ifdef _WIN32
        *(HWND*)(hWnd) = glfwGetWin32Window((GLFWwindow*)mWindow);
#endif
    }

    void Window::getX11Display(void* display) const
    {
#ifdef __linux__
        *(::Display**)(display) = glfwGetX11Display();
#endif
    }

    void Window::getX11Handle(void* window) const
    {
#ifdef __linux__
        // ::Window 显式取全局命名空间下 X11 的 Window(XID)类型,避免和 hyp::Window 这个类同名冲突
        *(::Window*)(window) = glfwGetX11Window((GLFWwindow*)mWindow);
#endif
    }

    void Window::getWaylandDisplay(void* display) const
    {
#ifdef __linux__
        *(struct wl_display**)(display) = glfwGetWaylandDisplay();
#endif
    }

    void Window::getWaylandSurface(void* surface) const
    {
#ifdef __linux__
        *(struct wl_surface**)(surface) = glfwGetWaylandWindow((GLFWwindow*)mWindow);
#endif
    }

}
