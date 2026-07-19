# HyperEngine

HyperEngine 是一个使用 C++20 编写的实验性实时 3D 引擎。项目围绕自研 RHI、RenderGraph、场景/组件框架和资产系统构建，目前的主要可运行入口是 `RenderTest`。

> [!IMPORTANT]
> 项目仍处于快速开发阶段，API、资产格式和渲染架构可能发生不兼容变更。当前默认且实际启用的图形后端是 Vulkan；WebGPU/Dawn 后端已进入代码库并参与编译，但还没有对外暴露运行时后端选择入口。

## 当前能力

### 渲染

- RHI 抽象层，包含 Vulkan 和 WebGPU/Dawn 两套后端实现
- 使用 DAG 追踪 Pass 与资源依赖，支持 Transfer、Compute、Graphics 和 Present Pass
- Pass 的添加与执行顺序由开发者组织，RenderGraph 不会自动重排
- RenderGraph 负责无效 Pass 裁剪、瞬态资源分配和资源屏障推导
- 默认使用逻辑线程 + 渲染线程；可通过 `HYP_SINGLE_THREADED_RENDERING` 切换为单线程渲染
- 延迟渲染管线：GBuffer、级联阴影、直接光照、大气散射、天空光 IBL、半透明和最终呈现
- 3D Gaussian Splatting：GPU 剔除、基数排序与绘制
- 视锥剔除、渲染标签、材质/几何排序以及多 View 数据管理
- Bindless 资源注册（设备不支持时使用兼容路径）
- Vulkan/WebGPU 共用的 ImGui 接入层

### 着色器与材质

- 使用 Slang 编写着色器
- 引擎内置 Pass Shader 在构建期通过 `slangc` 生成 SPIR-V/WGSL C++ 头文件
- 材质模板支持注入材质求值与顶点位移代码，并按 RenderTag、顶点布局等生成变体
- 管线、BindGroupLayout、Sampler 和材质 Shader 均有缓存层

### 场景、资产与运行时

- Scene / Entity / Component 框架，支持父子层级、Transform 依赖和分组 Tick
- Camera、MeshRenderer、DirectionalLight、Atmosphere、SkyLight、GSplatRenderer 等组件
- 反射、对象生命周期、序列化和输入映射基础设施
- `.asset` 二进制资产格式；启动时扫描 Header，正文按需加载
- `Engine/` 与 `Project/` 两套资产命名空间，使用 GUID 维护跨资产引用
- 纹理压缩与派生数据缓存
- glTF 2.0 / GLB 导入，可转换 Mesh、PBR Material 和 Texture；当前不重建 glTF Node 层级
- pybind11 嵌入式模块和 Python 扩展模块的初步绑定
- 基于 Jolt Physics 的物理场景、刚体和碰撞形状基础代码；尚未接入默认 `Engine` 主循环

## 架构概览

```text
Application (RenderTest)
        |
        v
Engine ── Input / Scene / AssetManager / Context
        |
        v
RenderFrontend ── RenderCommandQueue ── RenderBackend
                                             |
                  Renderer / RenderGraph <── FrameContext
                                             |
                                      RHI (Vulkan | WebGPU)
```

默认情况下，逻辑线程只更新场景并向命令队列提交轻量任务，GPU 资源和图形 API 对象由渲染线程持有和操作。启用单线程渲染选项后，同一套命令会在逻辑线程内立即消费，便于调试。

### 每帧执行流程

根据 GitNexus 对当前调用图和执行流的分析，主路径为：

```text
Engine::run
└─ Engine::tick
   ├─ InputSystem::tick
   ├─ RenderFrontend::beginFrame
   ├─ Scene::tick
   │  ├─ 处理 Entity / Component 注册、销毁与分组 Tick
   │  ├─ 更新 Transform 依赖
   │  └─ 提交 SceneProxy 渲染任务
   │     └─ SceneRenderer::render
   │        ├─ View 剔除与 DrawItem 排序
   │        ├─ Upload Pass
   │        ├─ Cascade Shadow Pipeline
   │        └─ Deferred Pipeline
   ├─ RenderFrontend::render
   │  └─ FrameContext::render
   │     ├─ RenderGraph::compile
   │     ├─ 获取/调整 Swapchain
   │     ├─ 更新全局帧数据
   │     ├─ RenderGraph::execute
   │     └─ Submit + Present
   └─ RenderFrontend::endFrame
```

相机 View 的典型 Pass 顺序如下。顺序由管线代码显式组织；Atmosphere、SkyLight、GSplat、Translucent 等 Pass 会按场景内容条件加入：

```text
Upload
Cascade Shadow Depth
Prepare
GBuffer
Atmosphere LUTs + Atmosphere
Direct Lighting
SkyLight Capture / SH / Prefilter
Indirect Lighting
GSplat Culling / Radix Sort / Render
Translucent
Present
```

## 目录结构

```text
HyperEngine/
├─ Asset/                         # 引擎序列化资产（Engine/ 命名空间）
├─ Resource/                      # RenderTest 使用的原始示例资源
├─ Shader/                        # Slang 着色器源码
├─ Intermediate/DerivedDataCache/ # 纹理、Dawn 等派生缓存
├─ CMake/                         # CMake 辅助脚本与 Pass Shader 代码生成
├─ Source/
│  ├─ Runtime/
│  │  ├─ Core/                    # 对象、反射、序列化、资产、数学与日志
│  │  ├─ Platform/                # GLFW 窗口与原生窗口句柄
│  │  ├─ RHI/                     # 后端无关图形接口
│  │  ├─ Vulkan/                  # Vulkan RHI 后端
│  │  ├─ WebGPU/                  # Dawn/WebGPU RHI 后端
│  │  ├─ RenderBase/              # 渲染线程、RenderGraph、代理与缓存
│  │  ├─ Renderer/                # 场景渲染器、管线与 Render Pass
│  │  ├─ PassShaders/             # 构建期 Shader 代码生成目标
│  │  ├─ HypImGui/                # ImGui 前后端桥接
│  │  ├─ Engine/                  # 场景、组件、材质、纹理、输入与物理
│  │  └─ PyHyperEngine/           # Python 绑定
│  ├─ Editor/
│  │  ├─ AssetInterchange/        # glTF/GLB 导入
│  │  └─ Editor/                  # 编辑器占位目标
│  └─ Test/RenderTest/             # 当前演示与集成测试入口
├─ ThirdParty/                    # 随仓库构建的第三方代码
├─ CMakeLists.txt
├─ CMakePresets.json
└─ vcpkg.json
```

## 构建

### 已验证环境

- Windows x64
- Visual Studio / MSVC，支持 C++20
- CMake 3.10 或更高版本
- Vulkan SDK
- vcpkg，并设置环境变量 `VCPKG_ROOT`

代码中已经包含 Linux 的 Xlib/Wayland 窗口接入，但仓库当前只提供通用的 `vcpkg` Preset，Linux 构建与运行尚未作为稳定路径验证。

### 配置与编译 RenderTest

```powershell
# VCPKG_ROOT 应指向 vcpkg 根目录
$env:VCPKG_ROOT = "C:\path\to\vcpkg"

cmake --preset vcpkg
cmake --build Build --config Debug --target RenderTest --parallel
```

Release 构建：

```powershell
cmake --build Build --config Release --target RenderTest --parallel
```

若需要在单线程中调试渲染命令：

```powershell
cmake --preset vcpkg -DHYP_SINGLE_THREADED_RENDERING=ON
cmake --build Build --config Debug --target RenderTest --parallel
```

> [!NOTE]
> 根工程中的 `Editor` 目前只有占位类，没有应用入口。请显式构建 `RenderTest`，不要将 `Editor` 视为可运行程序。

## 运行

Visual Studio 多配置生成器的 Debug 输出路径为：

```powershell
& .\Build\Source\Test\RenderTest\Debug\RenderTest.exe
```

Release 配置对应：

```powershell
& .\Build\Source\Test\RenderTest\Release\RenderTest.exe
```

`RenderTest` 当前覆盖了程序化网格与材质、资产加载、glTF/GLB、相机控制、方向光、大气、天空光、半透明以及 Gaussian Splatting 等路径。所需 Slang 和纹理压缩运行库会在构建后复制到可执行文件目录。

## 依赖

vcpkg Manifest 管理：

- GLFW、spdlog、GLM、nlohmann/json
- Vulkan Memory Allocator、Slang、Dawn
- stb、tinygltf、Basis Universal、tinyply、xxHash
- Python 3.12.13、pybind11
- Jolt Physics

随仓库构建：

- volk
- Dear ImGui
- ispc_texcomp
- vk_radix_sort

## 开发状态与限制

- Vulkan 是当前默认运行后端；WebGPU 后端仍处于对齐和验证阶段。
- Editor 尚未实现可执行入口和编辑工作流。
- Python 绑定只覆盖部分 Core/Engine API。
- 物理模块尚未由默认 Engine 生命周期驱动。
- `AssetManager::moveAsset` 和 `copyAsset` 仍为空实现。
- 项目暂未提供稳定 API、版本化资产迁移工具或完整自动化测试套件。
