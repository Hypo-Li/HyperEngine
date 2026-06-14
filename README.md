# HyperEngine

HyperEngine 是一个基于 Vulkan 的现代化三维渲染引擎，目前处于早期开发阶段。

> 注意：项目仍在快速迭代中，大量功能仅完成基础框架或占位实现，API 和结构可能随时变动。

---

## 项目结构

```
HyperEngine/
├── Source/
│   ├── Runtime/          # 引擎运行时
│   │   ├── Core/         # 基础类型、数学、反射、对象系统、日志
│   │   ├── Platform/     # 窗口抽象（GLFW）
│   │   ├── RenderBase/   # Vulkan 后端、渲染图、资源代理、着色器编译
│   │   ├── Renderer/     # 场景渲染器、渲染管线、渲染 Pass
│   │   ├── Engine/       # 实体/组件/场景框架、材质、输入、物理
│   │   └── PyHyperEngine/# Python 绑定（pybind11）
│   ├── Test/             # 测试程序
│   │   └── RenderTest/   # 渲染测试入口
│   └── Editor/           # 编辑器（占位）
├── Shader/               # Slang 着色器源码
├── Resource/             # 运行时资源
├── ThirdParty/           # 内置第三方库
├── CMakeLists.txt
├── CMakePresets.json
└── vcpkg.json
```

---

## 引擎如何跑完一帧

引擎的主循环位于 `Source/Runtime/Engine/Engine.cpp`，流程如下：

1. `Engine::run()` 进入主循环，计算 `deltaTime`。
2. `Engine::tick(deltaTime)` 依次执行：
   - 输入系统更新
   - 渲染前端开始帧：`RenderFrontend::beginFrame()`
   - 场景逻辑更新：`Scene::tick()`，并 enqueue 渲染工作
   - 渲染前端提交帧：`RenderFrontend::render()`
   - 渲染前端结束帧：`RenderFrontend::endFrame()`

### 逻辑 / 渲染双线程

- 主线程（逻辑线程）负责游戏逻辑、组件更新、向渲染命令队列提交轻量任务。
- 渲染线程从命令队列中取出任务并执行所有 GPU 工作。
- 两者通过单生产者单消费者（SPSC）渲染命令队列以及 `RenderFrameContext` 进行同步。

---

## 一帧的渲染流程

GPU 层面的每帧工作由 `RenderFrameContext` 在渲染线程中执行：

1. `beginFrame()`：创建新的 `RenderGraph`，回收上一帧暂存内存。
2. `render()`：
   - 编译渲染图：`RenderGraph::compile()`
   - 等待上一帧命令缓冲完成
   - 获取下一帧 Swapchain 图像
   - 开始录制主命令缓冲
   - 更新全局帧数据并绑定全局描述符集
   - 执行渲染图：`RenderGraph::execute()`
   - 提交命令缓冲并呈现
3. `endFrame()`：销毁渲染图，推进帧索引。

场景层在 `Scene::tick()` 中 enqueue 的渲染任务会实例化 `SceneRenderer`，由它构建当前帧的 `RenderGraph`，典型 Pass 顺序为：

```
Upload Pass
Cascade Shadow Depth Pass      （级联阴影深度）
GBuffer Pass                   （几何缓冲）
Direct Lighting Pass           （延迟光照）
Atmosphere Pass                （大气 LUT 与合成）
SkyLight Pass                  （天空光立方体贴图）
GSplat Pass                    （3D Gaussian Splatting）
Present Pass                   （Blit 到 Swapchain）
```

---

## 渲染系统是如何工作的

### 渲染图（RenderGraph）

`RenderGraph` 是引擎的核心调度结构，主要职责：

- 以 Pass 和资源节点组织一帧的 GPU 工作
- 基于 DAG 进行 Pass 剔除和依赖排序
- 自动推导资源屏障与图像布局
- 管理 transient 资源（每帧临时纹理/缓冲）

### Vulkan 抽象层

`RenderBase` 模块对 Vulkan 做了封装，核心对象包括：

- `Swapchain`：窗口呈现、图像获取、Resize 处理
- `CommandBuffer` / `CommandPool`：命令缓冲录制与复用
- `PipelineCache`：管线布局、图形/计算管线缓存
- `DescriptorSet`：描述符集分配与管理

### 资源代理

渲染资源通过代理对象管理，由 `ProxyManager` 统一分配：

- `TextureProxy`：GPU 图像，注册到 Bindless 表
- `GeometryProxy`：顶点/索引缓冲
- `MaterialProxy`：材质参数与 uniform buffer
- `SceneProxy` / `CameraProxy` / `MeshRenderProxy` 等：场景对象在渲染线程的表示

### 材质与着色器

- 着色器使用 Slang 编写，运行时由 `ShaderCompiler` 编译为 SPIR-V。
- 材质系统支持源码片段注入，根据 RenderTag 与顶点布局生成 Shader Variant。
- 渲染管线通过 `PipelineCache` 缓存已编译的 Vulkan 管线。

### 全局绑定

描述符集按以下分层设计：

- Set 0：全局数据 — Bindless 纹理、sampler、FrameData UBO
- Set 1：场景与视图 — instance 数据、light 数据、动态 view 数据
- Set 2：Pass / Material — 每 Pass 或每材质资源
- Set 3：预留

---

## 如何构建

### 环境要求

- Windows
- CMake 3.10 或更高版本
- MSVC 编译器
- Vulkan SDK
- vcpkg，并设置环境变量 `VCPKG_ROOT`

### 构建步骤

> 注意：`CMakePresets.json` 中目前只有 `vcpkg` 预设可正确编译。

```powershell
# 1. 确保已设置 VCPKG_ROOT 环境变量
# 2. 配置
cmake --preset vcpkg

# 3. 构建
cmake --build Build
```

构建输出位于 `Build/` 目录。

---

## 运行测试

`RenderTest` 是一个用于测试渲染管线的可执行程序，演示了程序化 Mesh、材质、相机控制、方向光、大气和 GSplat 等内容。

构建完成后，可运行：

```powershell
Build\Source\Test\RenderTest\RenderTest.exe
```

---

## 依赖

由 vcpkg 管理：

- `glfw3`
- `spdlog`
- `vulkan-memory-allocator`
- `shader-slang`
- `glm`
- `nlohmann-json`
- `stb`
- `tinygltf`
- `basisu`
- `python3`（固定 3.12.13）
- `pybind11`
- `joltphysics`

内置第三方库：

- `volk`
- `Dear ImGui`
- `ispc_texcomp`
- `vk_radix_sort`
