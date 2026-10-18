#pragma once

namespace hyp
{
    class Window;

    // GLFW_CURSOR_DISABLED 下 glfwGetCursorPos() 上报的是不受屏幕边界限制的虚拟坐标, 会飘到
    // 当前 OS 光标实际所在范围之外——如果这段时间还照常喂给 io.MousePos, ImGui 会拿这个虚拟
    // 坐标去测其它窗口的控件 hover, 表现成"明明没有鼠标在那儿, 别的面板却在高亮"。
    // ImGuiConfigFlags_NoMouse 让 ImGui 在这段时间不做任何 hover/点击判定(imgui.cpp 里
    // "Disabled mouse hovering" 那段), 但不影响 io.MousePos/MouseDelta 本身继续更新, 所以
    // 靠 delta 驱动的相机看方向/DragXXX 控件都不受影响。
    //
    // 捕获/释放光标要跟这个标记的开关严格配对, 统一走这个函数, 不要单独调用
    // Window::captureCursor()/releaseCursor()。
    void SetCursorCaptured(Window& window, bool captured);

    // DragFloat/DragScalarN 之类的拖拽控件是按每帧的相对位移(io.MouseDelta)累加算值的, 不是
    // 按点击起点到当前的绝对距离——这意味着拖拽期间只要捕获光标, 这些控件感知到的就是无限位移,
    // 不会因为鼠标撞上屏幕边缘就调不动了。松开鼠标后光标会回到拖拽开始时的位置(GLFW 在
    // Disabled<->Normal 之间切换的既有行为)。
    //
    // 必须紧跟在对应的 Drag 调用后面调用, 中间不能插入别的 ImGui item——内部用
    // IsItemActivated()/IsItemDeactivated() 查的是"上一个绘制的 item"的状态。
    //
    // 只对 DragXXX 系列有意义: SliderFloat/SliderInt 是按鼠标在滑条上的绝对位置算值的,
    // 撞到滑条边缘夹到 min/max 是它们设计上就该有的行为, 用在这类控件上没有效果。
    void CaptureCursorDuringDrag(Window& window);
}
