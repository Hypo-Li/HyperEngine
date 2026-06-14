#include "Window.h"
#include <Core/Common/LogSystem.h>

#include <GLFW/glfw3.h>
#ifdef WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#endif

namespace hyp
{
    static std::unordered_map<void*, Window*> gWindowMap;

    static std::unordered_map<void*, std::function<void(Window*, int, int)>> gResizeCallbackMap;
    static void resizeCallback(GLFWwindow* window, int width, int height)
    {
        auto it = gResizeCallbackMap.find(window);
        if (it != gResizeCallbackMap.end())
        {
            it->second(gWindowMap.at(window), width, height);
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

    Window::Window(int width, int height, const std::string& title) :
        mWidth(width), mHeight(height), mTitle(title)
    {
        if (gWindowMap.size() == 0)
        {
            glfwInit();
            glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
            glfwSetJoystickCallback(joystickCallback);
        }

        mWindow = glfwCreateWindow(mWidth, mHeight, mTitle.c_str(), nullptr, nullptr);
        glfwSetWindowSizeCallback((GLFWwindow*)mWindow, resizeCallback);
        glfwSetMouseButtonCallback((GLFWwindow*)mWindow, mouseButtonCallback);
        glfwSetCursorPosCallback((GLFWwindow*)mWindow, cursorPositionCallback);
        glfwSetScrollCallback((GLFWwindow*)mWindow, scrollCallback);
        glfwSetKeyCallback((GLFWwindow*)mWindow, keyCallback);
        gWindowMap.emplace(mWindow, this);

        glfwSetInputMode((GLFWwindow*)mWindow, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
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

    std::vector<const char*> Window::getRequiredInstanceExtensions()
    {
        uint32_t glfwExtensionCount = 0;
        const char** glfwExtensions;
        glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
        std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);
        return extensions;
    }

    void Window::pollEvents()
    {
        glfwPollEvents();
    }

    void Window::waitEvents()
    {
        glfwWaitEvents();
    }

    uint2 Window::getFramebufferSize() const
    {
        int width, height;
        glfwGetFramebufferSize((GLFWwindow*)mWindow, &width, &height);
        return uint2{ width, height };
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
#ifdef WIN32
        * (HWND*)(hWnd) = glfwGetWin32Window((GLFWwindow*)mWindow);
#endif
    }
}
