#pragma once
#include <imgui.h>

struct GLFWwindow;

namespace hyp
{
    // 警告: 不要在 hyp 命名空间下(或任何被 using 进来的地方)再定义一个叫 ImGui 的
    // 命名空间或类型。Dear ImGui 本身在全局作用域下有 `namespace ImGui { ... }`,
    // 所有现有代码里 `ImGui::NewFrame()`/`ImGui::Begin()` 这类非限定调用, 都是在
    // `namespace hyp { ... }` 内部依赖"当前作用域找不到叫 ImGui 的东西, 于是查到全局
    // 的 ::ImGui"这条查找规则解析的。一旦 hyp 内部出现同名的嵌套命名空间/类,
    // 非限定查找会优先命中它, 导致全项目所有 ImGui 调用编译失败。这也是这个模块的两个
    // 类分别叫 ImGuiFrontend/ImGuiBackend、而不是叫 ImGui 的原因。

    class InputSystem;
    class RenderBackend;

    // 逻辑线程侧的 ImGui 接入: Context/GLFW 平台后端的生命周期、每帧 NewFrame()/
    // Render(), 以及把 Render() 产出的 ImDrawData 转换成能安全跨线程传给渲染线程的
    // 指针(内部按 kMaxFramesInFlight 轮转一份 swap-based 的持久 snapshot, 见
    // ImGuiDrawDataSnapshot 的注释——比逐帧深拷贝更省, 代价是要求调用节奏和渲染线程的
    // 消费节奏保持严格对应, 具体不变量见 .cpp 里的注释)。
    //
    // 渲染后端(Vulkan/WebGPU)的资源初始化/销毁/绘制录制在 ImGuiBackend 里, 两者的
    // 划分方式和引擎里 RenderFrontend/RenderBackend 的逻辑线程/渲染线程划分完全对应。
    class ImGuiFrontend
    {
    public:
        // 只应在逻辑线程调用一次, Window 已创建之后。做 Context/GLFW 平台后端的初始化
        // (不接触任何 GPU 资源), 并向 inputSystem 注册一个 IInputEventHandler, 按
        // io.WantCaptureMouse/WantCaptureKeyboard 过滤掉 ImGui 想要的鼠标/键盘事件——
        // 这样"点在 ImGui 面板上"时, 场景相机/玩法逻辑的输入绑定从源头就收不到对应的
        // 按键/鼠标事件, 不需要下游再各自判断 WantCaptureMouse。
        static void init(GLFWwindow* window, RenderBackend& renderBackend, InputSystem& inputSystem);

        // 只应在逻辑线程调用一次, 进程退出前, 渲染线程已经完全停下、
        // ImGuiBackend::shutdown() 已经调用之后(顺序和 RenderBackend/Engine 的析构
        // 要求一致: 先停渲染线程和它持有的 GPU 资源, 再销毁 Context)。
        static void shutdown();

        // 逻辑线程, 每帧一次: NewFrame()。调用方在这之后自己构建 ImGui 面板
        // (Begin/End 等), 再调用 endFrameAndSnapshot()。如果渲染后端没有初始化成功
        // (见 ImGuiBackend::isAvailable()), 直接跳过——调用方这一帧不应该再调用任何
        // ImGui 的 Begin/Text/End 等接口, 否则会触发 ImGui 内部"字体图集必须已被后端
        // 认领"的断言。
        static void beginFrame();

        // 逻辑线程: Render() 并把结果 swap 进内部按帧轮转的持久 snapshot。返回的指针
        // 指向那个 snapshot 内部持久持有的 ImDrawData, 生命周期由 ImGuiFrontend 保证
        // 跨越渲染线程消费这一帧的全过程, 调用方(渲染线程侧的
        // ImGuiBackend::renderDrawData)不需要、也不应该尝试释放它。这一帧没有有效
        // 绘制数据时(比如窗口最小化, 或渲染后端未初始化成功)返回 nullptr。
        static ImDrawData* endFrameAndSnapshot();
    };
}
