# HyperEngine

HyperEngine 是一个使用 C++20 编写的实验性实时 3D 引擎。它包含自研 RHI 与 RenderGraph、基于反射的场景 / 组件框架、资产系统，以及一个基于 ImGui 的编辑器。

> [!IMPORTANT]
> 项目处于快速开发阶段，API、资产格式和渲染架构随时可能发生不兼容变更，暂无稳定版本，也没有资产版本迁移工具。各功能的成熟度见文末的[功能状态](#功能状态)。

## 目录

- [引擎能力](#引擎能力)
  - [场景与组件框架](#场景与组件框架)
  - [内置组件](#内置组件)
  - [资产](#资产)
  - [物理](#物理)
  - [输入](#输入)
- [渲染](#渲染)
  - [整体架构](#整体架构) · [RHI](#rhi) · [RenderGraph](#rendergraph) · [场景渲染](#场景渲染) · [材质与着色器](#材质与着色器) · [渲染管线](#渲染管线)
  - [实验性：Lumen 风格全局光照](#实验性lumen-风格全局光照)
- [编辑器](#编辑器)
- [功能状态](#功能状态)
- [项目结构](#项目结构)
- [构建与运行](#构建与运行)
- [第三方依赖](#第三方依赖)

## 引擎能力

### 场景与组件框架

**Scene / Entity / Component / Transform**

- `Entity` 之间通过 `Transform` 组成父子层级，支持 `setParent` / `addChild` / 同级顺序调整，并可选择重新挂接时是否保持世界变换（`ReparentMode`）。
- `Transform` 同时提供本地与世界两套 API（位置、旋转、欧拉角、缩放）。位置使用 `double3` 双精度，旋转使用四元数。
- `Transform` 可挂接到 `Skeleton` 上的命名 Socket，跟随骨骼动画运动。
- 每个 `Entity` 可设置可移动性（`Static` / `Dynamic`）与可见性，并可在运行时按类或类名添加、移除、重排组件。

**组件生命周期**

组件通过重写虚函数接入引擎：`onRegister` / `onUnregister` / `onBeginPlay` / `onTick` / `onEndPlay` / `onVisibilityChanged` / `onMobilityChanged`。`onTick` 按分组执行：`Pre_Physics` → `Physics` → `Post_Physics`。

**Play 状态**

`Scene` 支持 Stopped / Playing / Paused 三种状态以及单步执行。进入 Play 时才会创建物理世界并调用各组件的 `onBeginPlay`，停止时统一调用 `onEndPlay` 并销毁物理世界。编辑器的 Play / Stop 通过场景快照还原编辑状态。

**反射与序列化**

- 组件、资产、枚举与结构体接入统一的反射系统（`REFLECT_CLASS`），属性自动参与序列化、拷贝以及编辑器 Inspector。
- 序列化支持二进制存档与 Cook 后的紧凑格式。

**对象与引用**

对象使用引用计数（`TRef`），资产之间使用 GUID 与软引用（`SoftRef`）互相引用。

### 内置组件

| 类别 | 组件 | 能力 |
| --- | --- | --- |
| 相机 | `Camera` | 透视 / 正交投影，FOV、正交尺寸、近远裁剪面，水平 / 垂直投影轴，输出矩形，Culling Mask，优先级；可渲染到 `RenderTarget`，也可直接输出到窗口；相机级后处理设置（抗锯齿、AO、曝光、Bloom、Color Grading、色调映射，见[渲染管线](#渲染管线)） |
| 方向光 | `DirectionalLight` | 强度、颜色、色温；级联阴影（级联数、分辨率、阴影距离、分布指数、深度 / 斜率偏置、过渡与距离淡出、滤波质量）；可作为大气太阳 |
| 点光 / 聚光 | `PointLight`、`SpotLight` | 强度、颜色、色温、衰减半径、光源半径。**暂不支持阴影** |
| 天空光 | `SkyLight` | 光照来源可选“实时捕获天空”或“环境立方体贴图”；强度、颜色、预过滤分辨率、实时捕获开关 |
| 大气 | `Atmosphere` | 物理大气模型：地面反照率、星球 / 大气半径、Rayleigh / Mie / 臭氧散射与吸收参数、多重散射系数 |
| 天空背景 | `SkyBackground` | 使用 Sky 域材质绘制天空背景，可调强度 |
| 静态网格 | `StaticMeshRenderer` | 渲染 `StaticMesh`；逐 Submesh 覆盖材质、排序序号、是否投射阴影 |
| 骨骼网格 | `SkeletalMeshRenderer` | 渲染 `SkeletalMesh`；指定动画、播放 / 暂停、当前时间、播放速度；查询骨骼与 Socket 的世界变换；运行时覆盖单根骨骼的变换。**动画功能为实验性** |
| 精灵 | `SpriteRenderer` | 尺寸、UV 范围、水平 / 垂直翻转 |
| 文字 | `TextRenderer` | 使用 `Font`（SDF / MSDF / MTSDF），支持水平对齐 |
| 四边形基类 | `QuadRenderer` | 精灵与文字共用：材质、锚点、偏移、着色、Billboard（无 / 面向屏幕 / 绕轴）、按距离自动缩放（参考距离、最小 / 最大缩放）、深度偏置、排序、阴影 |
| 高斯泼溅 | `GSplatRenderer` | 渲染 `GSplat` 资产（3D Gaussian Splatting） |
| 碰撞体 | `BoxCollider`、`SphereCollider`、`CapsuleCollider`、`CylinderCollider`、`PlaneCollider`、`ConvexHullCollider`、`MeshCollider`、`CompoundCollider` | 见[物理](#物理)。**`ConvexHullCollider` 与 `CompoundCollider` 尚未测试** |
| 关节 | `HingeJoint` | 铰链约束：锚点、连接锚点、轴、角度上下限 |
| 预制体实例 | `PrefabInstance` | 记录来源 `Prefab`、节点映射与属性覆盖。**开发中** |

### 资产

资产以 `.asset` 二进制文件保存：启动时只扫描 Header，正文在需要时才加载。资产通过 GUID 互相引用，并分为两个命名空间：

- `Engine/`：引擎内置资产（默认位于 `Asset/`）
- `Project/`：项目资产（默认位于 `Project/Asset/`）

| 资产 | 说明 |
| --- | --- |
| `Scene` | 场景：Entity 与组件的完整序列化，可作为默认场景启动 |
| `Prefab` | 从一棵 Entity 子树创建，并可实例化回场景。**开发中** |
| `StaticMesh` | 多 LOD、多 Submesh、材质槽位；LOD 带屏占比阈值；可附带离线烘焙的距离场数据 |
| `SkeletalMesh` | 带蒙皮权重的网格，与 `Skeleton` 配合使用 |
| `Skeleton` | 骨骼层级（父子关系与绑定姿势）以及命名 Socket |
| `AnimationClip` | 逐骨骼的位置 / 旋转 / 缩放关键帧轨道，三个通道独立插值。**实验性** |
| `MaterialTemplate` | 使用 Slang 编写材质求值与顶点位移；可选材质域（Surface / Decal / Volume / Post Process / User Interface / Sky）、混合模式（Opaque / Masked / Translucent / Additive / Multiply）、着色模型（Unlit / Standard / Clear Coat / Cloth，或每像素由材质决定） |
| `MaterialInstance` | 基于模板覆盖参数；参数类型包括 Bool、Int、Float、Float2 / 3 / 4，以及 2D / 2D Array / 3D / Cube 纹理与字体 |
| `MaterialFunctionLibrary` | 可复用的 Slang 材质函数库 |
| `Texture2D` / `Texture2DArray` / `Texture3D` / `TextureCube` | 8 / 16 位整数与 16 / 32 位浮点像素格式；Mipmap；采样状态（Point / Bilinear / Trilinear / Anisotropic / 全局默认，Repeat / Clamp / Mirror 寻址）；支持 BC 压缩，预设为 Default / High Quality / Normal Map / Grayscale / HDR，压缩结果写入派生数据缓存（DDC），相同输入不重复压缩 |
| `RenderTarget` | 可被相机输出、可被材质采样的渲染目标 |
| `Font` | 由 TTF 烘焙出的 SDF / MSDF / MTSDF 字体图集（FreeType + HarfBuzz + msdfgen），支持字距 |
| `PhysicsMaterial` | 摩擦系数、恢复系数、密度。**尚未测试** |
| `ConvexHull` / `CompoundCollision` | 凸包与复合碰撞形状数据。**尚未测试** |
| `GSplat` | Gaussian Splat 点云数据 |

### 物理

物理基于 Jolt Physics（启用双精度）。

- **刚体类型**：`Static` / `Dynamic` / `Kinematic`。刚体行为由碰撞体组件自身的属性决定，不需要额外的 RigidBody 组件。
- **碰撞体属性**：Trigger 开关、物理层、质量（可覆盖）、质心偏移（可覆盖）、物理材质、本地中心偏移、线速度与角速度。
- **碰撞形状**：Box、Sphere、Capsule、Cylinder、Plane、ConvexHull、Mesh、Compound（其中 ConvexHull 与 Compound 尚未测试）。
- **作用力**：力、扭矩、冲量、作用于指定点的力 / 冲量、径向冲量（常数 / 线性衰减）。
- **事件**：Trigger 进入 / 离开（`ITriggerListener`）。
- **场景查询**：射线检测（`raycast`）与球形重叠查询（`overlapSphere`），支持按层掩码过滤。
- **物理层**：层列表与层间关系（`Ignore` / `Overlap` / `Block`）通过配置文件定义。
- **关节**：`HingeJoint`。

### 输入

`InputSystem` 与 `PlayerInput` 提供键盘、鼠标、手柄输入，并采用“绑定 → 动作”的映射方式：

- Action：触发类型为按下 / 释放 / 按住（可设时长阈值），支持修饰键；绑定到轴时可设方向与触发阈值
- 1D Axis、2D Axis，以及由多个按键组合而成的轴
- 2D Axis 可配置轴反转、灵敏度、死区与径向限幅

## 渲染

> [!NOTE]
> 渲染管线中的 PBR 光照与后处理仍需要在实际项目中进一步验证。

### 整体架构

- **前端 / 后端分离**：逻辑线程只更新场景，并通过命令队列向渲染线程提交轻量任务；GPU 资源和图形 API 对象只由渲染线程持有，逻辑线程通过 `ProxyHandle` 引用它们（网格、材质、纹理、相机、光源等都有对应的 Proxy）。
- **帧流水**：逻辑线程最多领先渲染线程 2 帧。定义 `HYP_SINGLE_THREADED_RENDERING` 后，同一套命令会在逻辑线程内立即消费，便于调试。
- **每帧流程**：`Scene::tick` 将场景同步为 SceneProxy → 编译 RenderGraph → 获取或调整 Swapchain → 更新全局帧数据 → 执行 RenderGraph → 提交并呈现。
- **深度约定**：主相机使用 Reversed-Z；级联阴影贴图独立使用正向 Z。

### RHI

- 后端无关的接口，覆盖 Device、CommandList、Buffer、Texture、Sampler、BindGroup、Pipeline、Swapchain、Fence 等。
- 提供 **Vulkan** 与 **WebGPU（Dawn）** 两套后端，通过 `config.json` 的 `RHIBackend` 选择。
- **特性协商**：每项特性可声明为 `Required` / `Optional` / `Ignored`，运行时解析为实际可用的特性集，涉及 BC 压缩、Subgroup、Float16 / Int64、浮点原子操作、Bindless、独立混合等。
- **Bindless**：提供 Bindless 资源注册表，设备不支持时走兼容路径。
- **缓存**：Pipeline、BindGroupLayout、Sampler 与 Shader 均有缓存层。
- **调试**：`HYP_RHI_COMMAND_RECORDING_IMMEDIATE` 可让 RHI 命令立即录制。

### RenderGraph

- 用 DAG 追踪 Pass 与资源依赖，Pass 类型为 Transfer / Compute / Graphics / Present。
- Pass 的添加顺序由管线代码显式组织，RenderGraph 不会重排。
- **编译阶段**：基于引用计数裁剪无人读取的 Pass（需要保留的 Pass 用 `markSideEffect` 标记），并推导资源状态与屏障。
- **瞬态资源**：从池中分配，连续多帧未使用才回收；波动较大的资源可单独调大回收冷却期。
- 支持导入外部资源、创建子纹理视图（如逐 Mip 视图），以及 Blackboard（Pass 之间按名字共享资源）。

### 场景渲染

- **多视图**：同一场景可同时渲染多个视图（相机、阴影级联、编辑器视口）；视图数较多时剔除并行执行。
- **剔除**：先按相机 Culling Mask 与物体 Layer 过滤，再对包围球与包围盒做视锥体剔除；点光 / 聚光的光源体积同样按视锥体剔除。
- **LOD**：网格按视图选择 LOD（依据屏占比阈值）。
- **DrawItem 与分桶**：剔除后的 DrawItem 按 RenderTag 分入 DepthOnly / Opaque / Translucent / ShadowDepth 桶。
  - 不透明桶按材质、顶点布局、几何排序，减少状态切换。
  - 半透明桶按排序序号与相机距离排序。
  - 相同几何的 DrawItem 合并为实例化绘制。
- **RenderTag**：材质通过 RenderTag（Opaque、Translucent、ShadowDepth、DepthOnly、SkyBackground 等）决定参与哪些 Pass，也允许注册自定义 Tag（编辑器的拾取 Pass 即通过此方式接入）。

### 材质与着色器

- 着色器统一使用 **Slang**。
- **材质模板拼接**：材质的参数结构、顶点位置偏移函数与 `evaluateMaterial` 会被拼接进各 RenderTag 的着色器模板，因此同一份材质代码可在 GBuffer、阴影、半透明等不同 Pass 中复用。
- **变体**：按 RenderTag、顶点布局（静态网格、骨骼网格、Quad 等）和宏组合生成着色器变体，并带有内容寻址的编译缓存。
- **共享着色器**：在仅依赖几何的 RenderTag（如阴影深度）下，静态、不透明、无顶点位置偏移的材质会自动合并为同一份着色器与管线，省去逐材质切换。
- **错误兜底**：材质编译失败时使用品红自发光的兜底材质，不会中断渲染。
- **内置 Pass Shader** 在构建期由 `slangc` 编译为 SPIR-V / WGSL，并生成 C++ 头文件。

### 渲染管线

当前管线为**延迟渲染**。每个相机视图的主要 Pass 顺序如下（Atmosphere、SkyLight、GSplat、Translucent 等 Pass 按场景内容条件加入）：

1. **Upload**：上传场景、视图与相机后处理历史等数据。
2. **级联阴影**：每个视图一次，所有级联共用阴影深度图集；静态网格与骨骼网格均可投射阴影。
3. **GBuffer**：法线（八面体编码）、基础色、金属度 / 粗糙度等、着色模型与各向异性、清漆法线，另外输出 Velocity、深度与模板。
4. **HZB**：由场景深度构建分层深度。
5. **ShadowMask**：每盏投影的方向光各生成一张屏幕空间阴影遮罩，支持 PCF 1×1 / 3×3 / 5×5。
6. **SSAO / GTAO**：按相机设置选择。
7. **直接光照**：
   - 方向光：全屏绘制，并按太阳光穿过大气的透过率衰减。
   - 点光 / 聚光：绘制光源体积几何（球 / 锥），相机位于体积内时退化为全屏三角形。**点光与聚光暂不支持阴影**。
8. **天空光间接光照**：漫反射（SH）与镜面反射（预过滤）。
9. **大气合成**。
10. **半透明**。
11. **GSplat**：GPU 剔除 → 基数排序 → 间接绘制。
12. **TAA → 曝光 → Bloom → Color Grading → 呈现**。

**大气（Atmosphere）**

- 预计算 Transmittance、MultiScattering、SkyView、AerialPerspective 四张 LUT。
- 合成公式为 `SceneColor × 透过率 + 内散射`，因此排在所有光照写入 SceneColor 之后。

**天空光（SkyLight）**

- 光照来源可选实时捕获天空（SkyBackground 或大气渲染到立方体贴图），或指定的环境立方体贴图。
- 捕获结果会生成 Mip 链，计算漫反射 SH 系数并做镜面预过滤；镜面部分配合预计算的 DFG LUT 使用。

**半透明（Translucent）**

- 半透明物体按排序序号与距离绘制。
- 光照使用半透明光照体积：点光 / 聚光与天空光被注入 SH 体积，半透明表面再从中采样。

**相机级后处理**（设置挂在 `Camera` 上）

| 项目 | 说明 |
| --- | --- |
| 抗锯齿 | TAA 或关闭；TAA 使用抖动、Velocity 与历史帧 |
| 环境光遮蔽 | SSAO 或 GTAO；强度、幂、半径、淡出距离，GTAO 另有质量档位（Low ~ Ultra） |
| 曝光 | 手动（光圈 / 快门 / ISO）或自动（直方图测光、EV 范围、百分位、适应速度）；曝光补偿 |
| Bloom | 降采样 / 上采样金字塔；强度、迭代次数、上采样半径 |
| Color Grading | 白平衡、通道混合、阴影 / 中间调 / 高光分区、CDL（Slope / Offset / Power）、对比度、自然饱和度、饱和度；生成 3D LUT |
| 色调映射 | Linear / ACES / UE Filmic（默认） |

**输出路由**：最终画面可输出到窗口 Swapchain，或相机的 `RenderTarget`（直接写入，或先渲染到瞬态纹理再拷贝）。

### 其他渲染相关

- **骨骼网格**：为每个蒙皮实例保留当前与上一帧的蒙皮结果，用于计算 Velocity。该功能为实验性。
- **精灵 / 文字**：以实例化 Quad 绘制，不使用顶点缓冲，由着色器按实例数据拉取顶点；文字使用 SDF / MSDF 字体图集。
- **3D Gaussian Splatting**：GPU 剔除、基数排序，按深度排序后绘制。

### 实验性：Lumen 风格全局光照

仓库中包含一套 Lumen 风格的全局光照实现，由 `SceneProxy` 的开关控制，涉及：

- Mesh Card 表面缓存与 Radiosity
- Global Distance Field 与 Mesh SDF
- Screen Probe Gather
- 屏幕空间 HZB 追踪
- 镜面反射追踪与降噪

它仍处于开发早期的实验阶段，**当前不可用**，请不要在实际项目中启用。各类调试可视化（场景可视化模式、线框、图标等）也仍在开发中。

## 编辑器

`Editor` 是当前唯一的可执行程序，基于 Dear ImGui：

- **面板**：Hierarchy（实体层级）、Inspector（每类组件有专用面板）、Asset Browser（带缩略图）、Log、Scene View（编辑相机、GPU 拾取）。
- **编辑工作流**：全局撤销 / 重做；Play / Pause / Step；场景保存；首次运行自动创建默认场景。
- **资产编辑器页签**：StaticMesh、Texture、MaterialTemplate、MaterialInstance、MaterialFunctionLibrary、Font。
- **材质模板编辑**：内置 Slang 代码编辑器（tree-sitter 语法高亮），通过 `slangd`（Slang 的语言服务器）提供诊断信息。
- **资源导入**：
  - glTF 2.0 / GLB：导入网格、骨骼蒙皮、动画、PBR 材质与纹理（不重建 glTF Node 层级）。
  - 2D 纹理与立方体纹理，包含 HDR / EXR。
  - 字体烘焙。
- **离线烘焙**：基于 Embree 构建 BVH，烘焙网格距离场与 Mesh Card。

## 功能状态

| 状态 | 功能 |
| --- | --- |
| 可用，仍需实际项目验证 | 场景 / 组件框架、资产系统、材质系统、纹理与字体、物理（除下方“尚未测试”项）、输入、编辑器基础工作流、延迟渲染管线（含 PBR 光照与后处理） |
| 实验性 | 骨骼动画（可导入并播放，后续需要正式化） |
| 尚未测试 | 物理材质（`PhysicsMaterial`）、凸包（`ConvexHull` / `ConvexHullCollider`）、复合碰撞体（`CompoundCollision` / `CompoundCollider`） |
| 开发中 | Prefab（`Prefab` / `PrefabInstance`）、可视化（线框、图标等调试绘制） |
| 早期实验，**当前不可用** | Lumen 风格全局光照：Mesh Card 表面缓存、Global Distance Field / Mesh SDF、Screen Probe Gather、镜面反射。相关代码与着色器已在仓库中，但仍处于开发早期，不要在实际项目中启用 |
| 尚未支持 | 点光 / 聚光阴影 |
| 部分支持 | `GSplat`：已有资产与渲染组件，编辑器暂无导入流程 |
| 部分支持 | Python 绑定（PyHyperEngine）：默认关闭，仅覆盖少量 Core / Engine API |
| 部分支持 | WebGPU 后端：仍在对齐与验证，Vulkan 为默认且主要验证的后端 |
| 部分支持 | Linux：代码中已有窗口与运行库拷贝适配，但尚未作为稳定平台验证 |

## 项目结构

```text
HyperEngine/
├─ Asset/                 # 引擎内置资产（Engine/ 命名空间）
├─ Project/Asset/         # 项目资产（Project/ 命名空间）
├─ Resource/              # 字体、LUT、HDR、示例模型等原始资源
├─ Shader/                # Slang 着色器源码
├─ CMake/                 # CMake 辅助脚本与 Pass Shader 代码生成
├─ Source/
│  ├─ Runtime/
│  │  ├─ Core/            # 对象、反射、序列化、资产管理、数学、日志
│  │  ├─ Engine/          # 场景、组件、材质、纹理、网格、动画、物理、输入
│  │  ├─ RHI/             # 后端无关的图形接口
│  │  ├─ Vulkan/          # Vulkan 后端
│  │  ├─ WebGPU/          # Dawn / WebGPU 后端
│  │  ├─ RenderBase/      # 渲染线程、RenderGraph、代理与缓存
│  │  ├─ Renderer/        # 场景渲染器、渲染管线与 Render Pass
│  │  ├─ PassShaders/     # 构建期 Shader 代码生成
│  │  ├─ Platform/        # GLFW 窗口
│  │  └─ PyHyperEngine/   # Python 绑定
│  └─ Editor/             # 编辑器，以及导入、烘焙、字体、语言服务等工具库
├─ ThirdParty/            # 随仓库构建的第三方代码
├─ CMakeLists.txt
├─ CMakePresets.json
└─ vcpkg.json
```

## 构建与运行

### 环境

- Windows x64（主要开发与验证平台）
- Visual Studio / MSVC，支持 C++20
- CMake 4.2.3 或更高版本
- Vulkan SDK
- vcpkg，并设置环境变量 `VCPKG_ROOT`

### 构建

```powershell
# VCPKG_ROOT 应指向 vcpkg 根目录
$env:VCPKG_ROOT = "C:\path\to\vcpkg"

cmake --preset vcpkg
cmake --build Build --config Debug --target Editor --parallel
```

可选 CMake 选项：

| 选项 | 说明 |
| --- | --- |
| `HYP_SINGLE_THREADED_RENDERING` | 渲染命令在逻辑线程内立即执行，便于调试 |
| `HYP_RHI_COMMAND_RECORDING_IMMEDIATE` | RHI 命令立即录制，便于调试 |
| `HYP_ENABLE_PYTHON_BINDINGS` | 构建 Python 绑定（需通过 vcpkg 编译 python3，耗时较长） |

### 运行

```powershell
& .\Build\Source\Editor\Editor\Debug\Editor.exe
```

构建 `Editor` 后，输出目录会自动生成开发用的 `config.json`，指向源码树中的 `Asset/`、`Project/Asset/`、`Resource/` 与 `Shader/`。若没有 `config.json`，这些目录默认解析为可执行文件旁的相对路径。

`config.json` 可配置的字段：

| 字段 | 说明 |
| --- | --- |
| `EngineAssetDir` / `ProjectAssetDir` / `ResourceDir` / `ShaderDir` | 资产、资源与着色器目录 |
| `WindowWidth` / `WindowHeight` / `WindowTitle` | 窗口参数 |
| `RHIBackend` | 图形后端：`"Vulkan"`（默认）或 `"WebGPU"` |
| `DefaultTextureFilter` / `MaxAnisotropy` / `MipLodBias` | 全局纹理采样设置 |
| `PhysicsLayers` | 物理层及层间关系 |
| `DefaultScene` | 编辑器启动时打开的默认场景；首次运行时自动创建并写回 |

## 第三方依赖

通过 vcpkg Manifest 管理：GLFW、spdlog、GLM、nlohmann/json、Vulkan Memory Allocator、Slang、Dawn、stb、tinyexr、tinygltf、tinyply、meshoptimizer、xxHash、zstd、libwebp、miniz、FreeType、HarfBuzz、msdfgen、tree-sitter、Embree、Jolt Physics；可选 Python 3.12 与 pybind11。

随仓库构建：volk、Dear ImGui、ispc_texcomp、rgbcx、dds_image、tree-sitter-slang、vk_radix_sort。
