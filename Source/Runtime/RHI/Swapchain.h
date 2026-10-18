#pragma once
#include "Texture.h"

namespace hyp::rhi
{
    enum class NativeWindowType
    {
        Headless,
        Win32,
        Xcb,
        Xlib,
        Wayland,
        Web,
    };

    struct NativeWindowInfo
    {
        NativeWindowType type;
        union
        {
            struct {
                void* hinstance;
                void* hwnd;
            } win32;

            struct {
                void* display;
                uint64_t window;
            } xlib;

            struct {
                void* connection;
                uint32_t window;
            } xcb;

            struct {
                void* display;
                void* surface;
            } wayland;

            struct {
                const char* canvasId;
                void* canvasHandle;
            } web;
        };
    };

    struct SwapchainDesc
    {
        NativeWindowInfo nativeWindow;
        uint32_t width = 0;
        uint32_t height = 0;
        bool vsync = false;
    };

    enum class SwapchainStatus
    {
        Success,
        Suboptimal,
        Outdated,
        Lost,
        Error,
    };

    class Context;
    class Swapchain
    {
    public:
        Swapchain(const Device& device) : mDevice(device) {}

        virtual ~Swapchain() = default;

        const Device& getDevice() const noexcept
        {
            return mDevice;
        }

        virtual SwapchainStatus acquireNextTexture() = 0;

        virtual TextureView* getCurrentTextureView() const = 0;

        virtual TextureFormat getSurfaceTextureFormat() const = 0;

        virtual uint2 getExtent() const = 0;

        virtual void resize(uint32_t width, uint32_t height) = 0;

        virtual SwapchainStatus present() = 0;

    protected:
        const Device& mDevice;
    };
}
