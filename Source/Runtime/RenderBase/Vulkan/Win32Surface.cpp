#include "Win32Surface.h"

namespace hyp::vk
{
    Win32Surface::Win32Surface(const Instance& instance, HINSTANCE hInstance, HWND hWnd) noexcept :
        Surface(instance)
    {
        // Need VK_KHR_surface and VK_KHR_win32_surface extensions when create instance
        VkWin32SurfaceCreateInfoKHR surfaceInfo = {};
        surfaceInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
        surfaceInfo.hinstance = hInstance;
        surfaceInfo.hwnd = hWnd;

        vkCreateWin32SurfaceKHR(mInstance.getHandle(), &surfaceInfo, nullptr, &mSurface);
    }

    //XcbSurface::XcbSurface(const Instance& instance, xcb_connection_t* connection, xcb_window_t window) noexcept :
    //    Surface(instance)
    //{
    //    // 需要在创建 Instance 时开启 VK_KHR_surface 和 VK_KHR_xcb_surface 扩展
    //    VkXcbSurfaceCreateInfoKHR surfaceInfo = {};
    //    surfaceInfo.sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR;
    //    surfaceInfo.pNext = nullptr;
    //    surfaceInfo.flags = 0;
    //    surfaceInfo.connection = connection;
    //    surfaceInfo.window = window;

    //    vkCreateXcbSurfaceKHR(mInstance.getHandle(), &surfaceInfo, nullptr, &mSurface);
    //}

    //WaylandSurface::WaylandSurface(const Instance& instance, wl_display* display, wl_surface* wlSurface) noexcept :
    //    Surface(instance)
    //{
    //    // 需要在创建 Instance 时开启 VK_KHR_surface 和 VK_KHR_wayland_surface 扩展
    //    VkWaylandSurfaceCreateInfoKHR surfaceInfo = {};
    //    surfaceInfo.sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR;
    //    surfaceInfo.pNext = nullptr;
    //    surfaceInfo.flags = 0;
    //    surfaceInfo.display = display;
    //    surfaceInfo.surface = wlSurface;

    //    vkCreateWaylandSurfaceKHR(mInstance.getHandle(), &surfaceInfo, nullptr, &mSurface);
    //}
}
