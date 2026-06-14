#pragma once
#define NOMINMAX
#include <Windows.h>
#undef hyper
#include "Surface.h"

namespace hyp::vk
{
    class Win32Surface : public Surface
    {
    public:
        Win32Surface(const Instance& instance, HINSTANCE hInstance, HWND hWnd) noexcept;
    };
}
