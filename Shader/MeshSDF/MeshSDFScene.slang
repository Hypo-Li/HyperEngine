// 测试性质的场景 SDF 可视化 Pass: 只画 SceneProxy 里找到的第一个带 MeshShapeProxy 的实例,
// 从 RenderBase::DistanceFieldBrickAtlas 管理的共享 brick atlas 里采样距离场做 sphere tracing。
// 跟 Shader/MeshSDF/SDFRender.slang(旧的独立 Test/MeshSDF 项目用的、对一整块稠密 Texture3D
// 采样)的区别: 这里的数据是稀疏存储的(brick 分块 + 间接索引), 采样前要先查间接索引表拿到
// 这个 brick 在 atlas 里的槽位, 而且槽位之间在 atlas 里彼此不相邻, 只能用三线性插值(brick
// 的 1 层重叠边界正好是三线性插值需要的量, 不够三次重建需要跨到相邻 brick 采样)。
#include "../Common/Binding.slang"
#include "../Common/MeshShapeCard.slang"
#include "../Common/SparseDistanceField.slang"

[[vk::binding(0, 3)]]
StructuredBuffer<InstanceData> sInstanceData;

// hyp::RenderBackend::getMeshShapeDataTable() 那张跨网格共享的 Mesh 资产级别数据表——
// SDF metadata + 间接索引表 + CardLookup + CardData 都在同一段区间里, 见
// hyp::MeshShapeDataTable 的注释。CardData 每张卡 7 个 uint32(center.xyz/halfExtents.xyz
// 用 asfloat() 重新解释, 最后一个是原始 uint 的 directionIndex), 见 hyp::MeshCard。
// CardLookup 是 6 个 uint32 位图, 见 hyp::MeshShapeProxy::getCardLookupOffset() 的注释。
[[vk::binding(0, 2)]]
StructuredBuffer<uint> MeshShapeData;

[[vk::binding(1, 2)]]
Texture3D<float> BrickAtlas;

[[vk::binding(2, 2)]]
SamplerState LinearSampler;

// CardPages 是全场景共享的一块 buffer(hyp::SurfaceCacheAtlas 持有), 同时存两种东西: 每个
// 实例每张卡的 ranges 记录(pageIndirectionOffset, pageCount.x, pageCount.y, 3 个 uint32,
// 见 hyp::MeshRenderProxy::CardPageRange)和它们各自索引的 flat 页槽位号 payload——两者
// 不再固定切成"前半 ranges/后半 payload"两段, 而是共用同一个连续下标空间, 各自按需申请
// 一段连续区间(hyp::SurfaceCacheAtlas::allocateCardPageTableSlots), 因为两类负载此消彼长
// (卡多页少 vs 卡少页多), 固定切分会让某一边白白浪费额度。这个实例的 ranges 记录从哪开始,
// 由 PC.cardRangeSlotBase 显式给出; ranges 里的 pageIndirectionOffset 本身就是 payload
// 在这个共享空间里的绝对下标, 不需要再加任何固定偏移换算。
[[vk::binding(3, 2)]]
StructuredBuffer<uint> CardPages;

[[vk::binding(4, 2)]]
Texture2D<float4> CardAlbedoAtlas;

[[vk::binding(5, 2)]]
Texture2D<float> CardDepthAtlas;

// kSparseBrickSize/kSparseUniqueBrickSize/kInvalidSparseBrickIndex 现在定义在
// ../Common/SparseDistanceField.slang 里, kSurfaceCachePageSize/kSurfaceCacheAtlasResolution
// 定义在 ../Common/MeshShapeCard.slang 里, 这里不再重复声明

// push constant 总大小压在 64 字节以内(引擎里 rhi::kImmediateDataSize 的硬上限, 见
// Vulkan/Pipeline.cpp), 所以这里手动按 HLSL/Slang cbuffer 打包规则把 float3/int3 和紧
// 跟在后面的一个标量拼进同一个 16 字节槽位, 不逐个凑成 float4/int4——brickGridResolution
// 干脆不传, 在 sampleDistanceSparse 里现算(ceil(resolution / kSparseUniqueBrickSize)),
// 反正只是几条整数指令, 没必要为了它多占一整个 16 字节槽位。resolution 后面原来跟了一个
// 纯填充用的 pad0(凑满 16 字节槽位), 接 surface cache 采样时刚好用上: cardCount 是这个
// mesh 的卡片总数(遍历 CardLookup 位图时当上限用)。atlasBrickGridExtent 只存 1 个分量
// (标量, 不是 int3): brick atlas 强制要求是立方体(DistanceFieldBrickAtlas::
// chooseBrickGridExtent() 三个分量恒等), 三个分量各存一遍纯属浪费。cardRangeSlotBase 是
// 这个实例的 card ranges 记录在 CardPages 共享表里的起始下标(见上面 CardPages 的注释)——
// 合并成场景级共享 buffer 之后不能再假设"这个实例的数据从 buffer 第 0 个位置开始", 必须
// 显式传。indirectionTableOffset 同理: 这个网格自己的间接索引表在 MeshShapeData 这张
// 跨网格共享表里的起始下标(见 hyp::MeshShapeProxy::getIndirectionTableOffset())。C++
// 那边(MeshSDFPass.cpp::MeshSDFPushConstants)必须逐字段保持完全一致的偏移, 目前总大小
// 60 字节, 还有 4 字节余量。
struct MeshSDFPushConstants
{
    float3 boundsMin;
    float narrowBandDistance;
    float3 boundsMax;
    uint instanceIndex;
    int3 resolution;               // 稠密体素分辨率
    int cardCount;
    int atlasBrickGridExtent;      // atlas 每根轴排了多少个 brick(不是体素数); 强制立方体, 只存 1 个分量
    uint cardRangeSlotBase;        // 这个实例的 card ranges 记录在 CardPages 共享表里的起始下标
    uint indirectionTableOffset;   // 这个网格自己的间接索引表在 MeshShapeData 共享表里的起始下标
};
[[vk::push_constant]]
MeshSDFPushConstants PC;

struct VertexOutput
{
    float4 svPosition : SV_Position;
};

static const float2 kPositions[3] = { float2(-1, -3), float2(3, 1), float2(-1, 1) };

// 调试可视化开关: 打开后 FragmentMain 最终输出的不是采样颜色, 而是这个像素总共调了多少次
// sampleDistanceSparse(粗步进 + 二分细分 + 法线估算三处调用点全部算在内)映射出的冷热色带,
// 用完记得关掉换回正常输出
static const bool kVisualizeBrickSampleCount = false;
// 映射到色带最热端(纯红)对应的采样次数——没命中的像素粗步进最多能跑到 kMaxSteps(128)次,
// 但绝大多数有意义的差异(掠射角命中导致二分细分次数增加、未命中像素穿过稀疏区域提前退出
// 等)都出现在远小于 128 的范围, 封顶给低一点色带的对比度才够看, 需要看清 128 附近的差异
// 可以调大
static const float kBrickSampleCountVisualizationMax = 64.0;

// gBrickSampleCount 是 fragment shader 里的 per-invocation 全局变量(每个像素各自一份初始为 0
// 的拷贝, 不是跨像素共享的真正静态状态)——在 sampleDistanceSparse() 内部自增, 这样粗步进、
// 二分细分、estimateNormalOS 里各自独立调用它的地方都不用改签名、不用手动传引用计数,
// 一处自增就能覆盖全部调用点
static int gBrickSampleCount = 0;

// t∈[0,1] -> 冷(蓝) - 青 - 绿 - 黄 - 热(红)的经典热力图色带, t 越大代表采样次数越多
float3 heatColor(float t)
{
    t = saturate(t);
    const float3 kBlue = float3(0.0, 0.0, 1.0);
    const float3 kCyan = float3(0.0, 1.0, 1.0);
    const float3 kGreen = float3(0.0, 1.0, 0.0);
    const float3 kYellow = float3(1.0, 1.0, 0.0);
    const float3 kRed = float3(1.0, 0.0, 0.0);

    if (t < 0.25)
        return lerp(kBlue, kCyan, t / 0.25);
    if (t < 0.5)
        return lerp(kCyan, kGreen, (t - 0.25) / 0.25);
    if (t < 0.75)
        return lerp(kGreen, kYellow, (t - 0.5) / 0.25);
    return lerp(kYellow, kRed, (t - 0.75) / 0.25);
}

[shader("vertex")]
VertexOutput VertexMain(in uint vertexID : SV_VertexID)
{
    VertexOutput output;
    output.svPosition = float4(kPositions[vertexID], 0, 1);
    return output;
}

// 局部空间点 -> 距离值, 沿射线连续采样——实际的单物体稀疏 brick 查询逻辑在
// ../Common/SparseDistanceField.slang 里(跟 GDF 合成 compute shader 共用), 这里只负责
// 从 PC 组出 SparseDistanceFieldObject、维护采样计数这个可视化专属状态。
float sampleDistanceSparse(float3 positionOS)
{
    gBrickSampleCount += 1;

    SparseDistanceFieldObject obj;
    obj.boundsMin = PC.boundsMin;
    obj.boundsMax = PC.boundsMax;
    obj.narrowBandDistance = PC.narrowBandDistance;
    obj.resolution = PC.resolution;
    obj.atlasBrickGridExtent = PC.atlasBrickGridExtent;
    obj.indirectionTableOffset = PC.indirectionTableOffset;

    return sampleSparseDistanceField(obj, MeshShapeData, BrickAtlas, LinearSampler, positionOS);
}

bool intersectObjectBounds(float3 origin, float3 dir, out float tEnter, out float tExit)
{
    float3 invDir = 1.0 / dir;
    float3 t0 = (PC.boundsMin - origin) * invDir;
    float3 t1 = (PC.boundsMax - origin) * invDir;
    float3 tMin = min(t0, t1);
    float3 tMax = max(t0, t1);
    tEnter = max(max(tMin.x, tMin.y), tMin.z);
    tExit = min(min(tMax.x, tMax.y), tMax.z);
    return tExit >= max(tEnter, 0.0);
}

// 中心差分估法线, 步长逐轴取半个体素——跟 UE CalculateMeshSDFGradient()
// (DistanceFieldLightingShared.ush)一致: 网格只留了 kMeshDistanceFieldObjectBorder=1
// 个体素的边界, 超过半个体素往外插值就会越出这份边界, 读到边界外未定义的内容。
//
// 注意: 这里用有限差分不是因为"懒得算精确梯度", 而是刻意要的平滑效果, 别改成解析梯度——
// 三线性重建出来的场只有 C0 连续, 它的精确偏导在每个体素格边界上是跳变的(g.x 只随
// (fy,fz) 变化、在 fx 方向是常数, 另外两个分量同理), 也就是说重建出来的等值面在每个格
// 边界上真的有一道折痕。实测过: 换成数学上精确的三线性解析梯度, 画面会退化成一格一块的
// 明显方块, 比这里的差分难看得多。跨越格边界做差分相当于顺带做了一次低通, 把这些折痕抹
// 平, 视觉上才自然。
//
// 想从根上消除折痕只有两条路: 提高烘焙分辨率(格子小到折痕看不见), 或者换 C1 连续的重建
// (三次 B 样条)。后者当前的 brick 格式支持不了——三次重建需要 ±1.5 个体素的邻域, 而 brick
// 只有 1 层重叠边界(kSparseBrickSize 8 = kSparseUniqueBrickSize 7 + 1), 靠近块边界的采样点
// 会跨到相邻 brick, 而相邻 brick 在 atlas 里并不相邻。要支持得先改 brick 的重叠层数。
//
// 逐轴分别除以自己的 h: 三个轴的体素尺寸可以不相等(见 bakeMeshDistanceField 里逐轴独立
// clamp 分辨率的注释), 不除的话得到的是 (2h_x·∂f/∂x, 2h_y·∂f/∂y, 2h_z·∂f/∂z), 只有三轴
// h 相等时才和真正的梯度同向。
float3 estimateNormalOS(float3 positionOS, float3 h)
{
    float dx = sampleDistanceSparse(positionOS + float3(h.x, 0, 0)) - sampleDistanceSparse(positionOS - float3(h.x, 0, 0));
    float dy = sampleDistanceSparse(positionOS + float3(0, h.y, 0)) - sampleDistanceSparse(positionOS - float3(0, h.y, 0));
    float dz = sampleDistanceSparse(positionOS + float3(0, 0, h.z)) - sampleDistanceSparse(positionOS - float3(0, 0, h.z));
    return normalize(float3(dx / h.x, dy / h.y, dz / h.z));
}

[shader("fragment")]
float4 FragmentMain(in VertexOutput input) : SV_Target
{
    const float3 backgroundColor = float3(0.05, 0.05, 0.08);

    InstanceData instanceData = sInstanceData[PC.instanceIndex];

    float3 offset = float3(
        instanceData.objectToWorld.RSOffset[0].w,
        instanceData.objectToWorld.RSOffset[1].w,
        instanceData.objectToWorld.RSOffset[2].w
    );
    float3 relativePos = float3(instanceData.objectToWorld.tile - uViewData.cameraOrigin.tile) * WORLD_TILE_SIZE
        + (offset - uViewData.cameraOrigin.offset);
    float3 T = mul(relativePos, uViewData.worldToViewRot);
    float3x3 viewToObjectRS = mul(transpose(uViewData.worldToViewRot), instanceData.worldToObjectRS);

    float2 uv = input.svPosition.xy / uViewData.resolution.xy;
    float4 clipSpace = float4(uv.x * 2.0 - 1.0, (1.0 - uv.y) * 2.0 - 1.0, 0.5, 1.0);
    float4 viewSpace = mul(clipSpace, uViewData.clipToViewUnjittered);
    viewSpace *= 1.0 / viewSpace.w;
    float3 rayDirVS = normalize(viewSpace.xyz);

    // 相机在 view 空间原点, 直接用 -T 变换过去就是物体空间下的相机位置
    float3 rayOriginOS = mul(-T, viewToObjectRS);
    float3 rayDirOS = normalize(mul(rayDirVS, viewToObjectRS));

    float tEnter, tExit;
    if (!intersectObjectBounds(rayOriginOS, rayDirOS, tEnter, tExit))
        return float4(backgroundColor, 1.0);

    float3 voxelSize = (PC.boundsMax - PC.boundsMin) / float3(PC.resolution);
    float surfaceEpsilon = min(min(voxelSize.x, voxelSize.y), voxelSize.z) * 0.1;

    const int kMaxSteps = 128;
    float t = max(tEnter, 0.0);
    float prevT = t;
    bool hit = false;
    float3 positionOS = rayOriginOS;

    for (int i = 0; i < kMaxSteps; ++i)
    {
        positionOS = rayOriginOS + rayDirOS * t;
        float d = sampleDistanceSparse(positionOS);
        if (d < surfaceEpsilon)
        {
            hit = true;
            break;
        }
        prevT = t;
        t += max(d, surfaceEpsilon);
        if (t > tExit)
            break;
    }

    if (!hit)
    {
        if (kVisualizeBrickSampleCount)
            return float4(heatColor(float(gBrickSampleCount) / kBrickSampleCountVisualizationMax), 1.0);
        return float4(backgroundColor, 1.0);
    }

    // 粗步进用的 d 是"离表面最近点的距离", 不是"沿光线方向到表面的距离"——光线以掠射角
    // (接近平行于表面)命中时, d 判定为"够近"的那一刻, 沿光线方向实际已经越过真实交点
    // 一段距离。对薄壁 card(比如 0.1m 厚的墙)来说, 这段越界量很容易超过 card 自身的深度
    // 半宽, 表现为命中点在 sampleSurfaceCacheColor 里被所有候选卡的深度 AABB 测试拒绝、
    // 退化成灰色兜底, 呈现为以视轴为中心、固定掠射角阈值的一圈环(角度阈值跟距离无关, 所以
    // 环在视口里的占比不随相机远近变化)。
    //
    // 修法是把命中点沿光线方向收紧, 不是放宽 AABB 容差去迁就这个误差——放宽容差只是让
    // 各处的误判阈值一起变松, 掩盖不了真正的越界量, 还会让本该被拒绝的候选卡更容易混进来。
    // [prevT, t] 区间保证前者在真实交点之前(上一步 d >= surfaceEpsilon)、后者在之后,
    // 对这一小段做二分, 收紧到更贴近真实交点沿光线方向的位置
    {
        float tLow = prevT;
        float tHigh = t;
        // 区间本身够窄(远小于薄壁 card 的深度半宽)就没必要再二分下去了——正对着相机的
        // 命中点粗步进阶段区间往往已经很窄, 用不上 6 次全跑完; 6 次是给掠射角这种区间本来
        // 就宽的最坏情况留的余量, 不是每个像素都要付这个成本
        const float kRefineConverged = surfaceEpsilon * 0.1;
        for (int refineStep = 0; refineStep < 6 && (tHigh - tLow) > kRefineConverged; ++refineStep)
        {
            float tMid = 0.5 * (tLow + tHigh);
            float dMid = sampleDistanceSparse(rayOriginOS + rayDirOS * tMid);
            if (dMid < surfaceEpsilon)
                tHigh = tMid;
            else
                tLow = tMid;
        }
        t = tHigh;
        positionOS = rayOriginOS + rayDirOS * t;
    }

    float3 normalStep = max(voxelSize * 0.5, float3(1e-5));
    float3 normalOS = estimateNormalOS(positionOS, normalStep);

    // 从 surface cache(SurfaceCacheAtlas 捕获出来的 albedo)采样这个命中点的颜色——目前只是
    // 直接把采样结果当最终颜色输出, 不做光照(跟 CardCapture.slang 捕获阶段本身就没有光照
    // 是一致的, 这里只验证"采样对不对", 光照留到下一步)
    // CardLookup/CardData 用的 offset 不再单独传 push constant 字段——从已有的
    // indirectionTableOffset 反推出这个网格在 MeshShapeDataTable 里的区间起始下标(64
    // 字节预算已经很紧张, 见 MeshSDFPushConstants 的注释)
    uint meshShapeDataOffset = PC.indirectionTableOffset - kMeshShapeMetadataWordCount;
    bool cardHit;
    // 跟 UE per-mesh 追踪路径(LumenSoftwareRayTracing.ush 的 EvaluateRayHitFromSurfaceCache
    // 调用点)一致, 固定传一个不依赖体素大小的常数, 不能传 0(会让 MeshShapeCard.slang 里的
    // depthBiasFalloff 除零)
    const float kMeshSDFSurfaceCacheBias = 0.2;
    float3 color = sampleMeshShapeSurfaceCacheColor(
        MeshShapeData, CardPages, CardAlbedoAtlas, CardDepthAtlas, LinearSampler,
        meshShapeDataOffset, PC.resolution, PC.cardCount, PC.cardRangeSlotBase,
        positionOS, normalOS, kMeshSDFSurfaceCacheBias, instanceWorldScale(instanceData), cardHit);

    if (kVisualizeBrickSampleCount)
        return float4(heatColor(float(gBrickSampleCount) / kBrickSampleCountVisualizationMax), 1.0);

    return float4(color, 1.0);
}
