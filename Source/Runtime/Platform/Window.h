#pragma once
#include <string>
#include <vector>
#include <functional>

#include <Core/Common/Refable.h>
#include <Core/Math/Vector.h>

namespace hyp
{
    class Window : public Refable
    {
    public:
        Window(int width, int height, const std::string& title);
        ~Window();

        static std::vector<const char*> getRequiredInstanceExtensions();

        static void pollEvents();

        static void waitEvents();

        uint2 getFramebufferSize() const;

        double2 getCursorPosition() const;

        double2 getScrollOffset() const;

        float2 getContentScale() const;

        bool shouldClose() const;

        void setResizeCallback(std::function<void(Window*, int, int)>&& callback);

        void setMouseButtonCallback(std::function<void(Window*, int, int, int)>&& callback);

        void setCursorPositionCallback(std::function<void(Window*, double, double)>&& callback);

        void setScrollCallback(std::function<void(Window*, double, double)>&& callback);

        void setKeyCallback(std::function<void(Window*, int, int, int, int)>&& callback);

        static void setJoystickCallback(std::function<void(int, int)>&& callback);

        void getGlfwHandle(void* glfwWindow) const;

        void getWin32Handle(void* hWnd) const;

    private:
        int mWidth, mHeight;
        std::string mTitle;
        void* mWindow;
    };
}
