#pragma once
#include <volk.h>

#ifdef __linux__
// volk.h(通过 vulkan.h 的 VK_USE_PLATFORM_XLIB_KHR 路径)间接引入了 <X11/Xlib.h>,
// 这个头文件会把 None/Bool/Status/True/False/Success 定义成裸整数宏, 和引擎里同名的
// enum class 枚举值(比如 BufferUsage::None, ObjectFlags::None)以及标识符直接冲突,
// 用完立刻 undef 掉。所有 Vulkan 模块的文件都应该 include 这个头文件而不是直接
// include <volk.h>, 保证这个 undef 总是在 volk.h 展开之后、其余代码解析之前生效。
#undef None
#undef Bool
#undef Status
#undef True
#undef False
#undef Success
#undef Always
#endif
