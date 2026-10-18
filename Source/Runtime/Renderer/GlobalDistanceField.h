#pragma once
#include <Core/Common/Refable.h>
#include <Core/Math/Vector.h>
#include <RenderBase/ProxyHandle.h>
#include <RenderBase/StagingAllocator.h>
#include <RHI/Buffer.h>
#include <RHI/CommandList.h>

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace hyp
{
    class MeshRenderProxy;
    class DistanceFieldBrickAtlas;
    class RenderBackend;

    inline constexpr int32_t kGlobalDistanceFieldCascadeCount = 4;

    // 每级 cascade 的 brick 网格边长(不是体素数)——复用 kDistanceFieldBrickSize(8)/kDistanceFieldUniqueBrickSize(7),
    // 36*7=252, 对齐 UE Lumen Global SDF 的默认有效分辨率(GLumenSceneGlobalDFResolution=252)
    inline constexpr int32_t kGlobalDistanceFieldBricksPerAxis = 36;

    inline constexpr int32_t kGlobalDistanceFieldCellsPerCascade =
        kGlobalDistanceFieldBricksPerAxis * kGlobalDistanceFieldBricksPerAxis * kGlobalDistanceFieldBricksPerAxis;

    // 跟 UE GGlobalDistanceFieldInfluenceRangeInVoxels 一致——候选剔选按"物体到这一格的
    // 距离 < 这么多个体素"留容差, 不是精确相交, 否则格子网格跟着相机重新对齐时, 贴着物体
    // 包围盒边缘的格子会因为几何上刚好擦边而漏判候选
    inline constexpr int32_t kGlobalDistanceFieldInfluenceRangeInVoxels = 4;

    // Layer::pendingHeaderWords 里每个待合成 brick 的定长 header 记录(CPU 端剔选阶段的
    // 中间格式, 相对这一层自己的 pendingPayloadWords): { brickIndex(atlas 槽位号),
    // candidateCount, payloadOffset, cellCoord.x, cellCoord.y, cellCoord.z }
    inline constexpr uint32_t kGlobalDistanceFieldCompositeHeaderWordCount = 6;

    // GlobalDistanceFieldCompositePass 把静态/动态各自一批待合成层拼成一份统一 buffer
    // 传给 compute shader(上传进全局共享的 sIndices, 见 GlobalDistanceFieldComposite.slang), 布局:
    //   [cascade 信息表: 固定 kGlobalDistanceFieldCascadeCount 条, 按 cascadeIndex 直接索引]
    //   [合并后的 brick header 列表: 每条 kGlobalDistanceFieldCombinedHeaderWordCount 字]
    //   [合并后的 payload: 每条 kGlobalDistanceFieldCompositePayloadWordCount 字]
    // 跟 Shader/GlobalDistanceField/GlobalDistanceFieldComposite.slang 里的同名常量必须
    // 保持一致

    // cascade 信息表每条记录: gridMinTile.xyz(3) + gridMinOffset.xyz(3, asfloat 重新解释) +
    // cellSize(1, asfloat) + staticLayerIndirectionOffset(1, 只有动态 pass 有意义, 静态
    // pass 填 0)
    inline constexpr uint32_t kGlobalDistanceFieldCascadeInfoWordCount = 8;
    inline constexpr uint32_t kGlobalDistanceFieldCascadeInfoTableWordCount =
        static_cast<uint32_t>(kGlobalDistanceFieldCascadeCount) * kGlobalDistanceFieldCascadeInfoWordCount;

    // 合并后的 brick header 记录: 在 Layer::pendingHeaderWords 的 6 字格式前面多插一个
    // cascadeSlot(0~kGlobalDistanceFieldCascadeCount-1, 查 cascade 信息表用), payloadOffset
    // 改成相对合并后 payload 段的绝对偏移(不再是相对这一层自己的 payload)
    inline constexpr uint32_t kGlobalDistanceFieldCombinedHeaderWordCount =
        kGlobalDistanceFieldCompositeHeaderWordCount + 1;

    // 跟 GlobalDistanceFieldObjectGrid.slang 必须保持一致
    inline constexpr uint32_t kGlobalDistanceFieldObjectGridCellsPerBrickAxis = 4;
    inline constexpr uint32_t kGlobalDistanceFieldObjectGridCellsPerBrick =
        kGlobalDistanceFieldObjectGridCellsPerBrickAxis * kGlobalDistanceFieldObjectGridCellsPerBrickAxis * kGlobalDistanceFieldObjectGridCellsPerBrickAxis;
    inline constexpr uint32_t kGlobalDistanceFieldObjectGridCellCapacity = 4;
    inline constexpr uint32_t kGlobalDistanceFieldObjectGridWordsPerBrick = kGlobalDistanceFieldObjectGridCellsPerBrick * kGlobalDistanceFieldObjectGridCellCapacity;
    inline constexpr float kGlobalDistanceFieldObjectGridCardInterpolationRangeInVoxels = 3.0f;

    // 每个候选物体: meshInstanceIndex + 64 位掩码(低/高 32 位), 第 i 位表示第 i 个 Object Grid 格子离物体包围盒不超过 card 插值范围
    inline constexpr uint32_t kGlobalDistanceFieldCompositePayloadWordCount = 3;

    // payload 里 cardRangeSlotBase 那个字的"这个实例还没有可用 card ranges"哨兵值——
    // MeshRenderProxy::hasCardRanges() 为 false 时 mCardRangeSlotBase 可能是陈旧值(对象池
    // 复用留下的), 不能直接存, 存这个哨兵, 消费方(命中查询/采色阶段)看到就知道跳过这个
    // 候选的 card 采样、退化成兜底颜色, 跟 kInvalidDistanceFieldBrickIndex 是同一个思路
    inline constexpr uint32_t kGlobalDistanceFieldInvalidCardRangeSlotBase = 0xFFFFFFFFu;

    // 最内层 cascade 的半径(米)——UE Lumen 默认值 2500cm 换算成米, 往外每级翻倍
    inline constexpr float kGlobalDistanceFieldInnermostHalfExtent = 25.0f;

    // 场景开启 Lumen 时, 场景里每个相机各自持有一份合成好的多级 distance field clipmap——
    // clipmap 有一个中心, 多个视图没法共用同一个中心还都保持近处高分辨率, 所以不能做成
    // 场景级共享单例, 只能每个相机各建一份。体素数据物理上存在 LumenScene::
    // getGlobalDistanceFieldBrickAtlas() 这个场景级共享稀疏 brick 池里, 这里的 Cascade 只存索引,
    // 不会因为每个相机一份就把体素数据也复制成多份
    class GlobalDistanceField
    {
    public:
        struct Layer
        {
            // 这一层的间接索引表内容——最终会拼进 GlobalDistanceField::mIndirectionBuffer
            // 里固定大小的一段区间(见 getLayerIndirectionOffset()), 不是自己独立持有 buffer:
            // 4 级 cascade × 静态/动态 2 层一共 8 层, 每层大小恒定(kGlobalDistanceFieldCellsPerCascade)、
            // 永远同生共死, 不存在"某层单独变大/单独创建销毁"的情况, 不需要为此引入
            // RangeAllocator, 编译期固定偏移量就够
            std::vector<uint32_t> indirectionCPU;

            // indirectionCPU 里不是 kInvalidDistanceFieldBrickIndex 的全部 cellIndex, 升序
            std::vector<uint32_t> occupiedCells;

            // 标脏后触发的是整个 cascade 重算, 不是只重算相机移动新暴露出来的那部分格子——
            // 当前场景规模下足够, 场景变大、整级重算开销顶不住时再升级成环形寻址的增量更新
            bool dirty = true;

            // updateCascadeLayer() 边剔选候选物体边拍平成这两份 CPU 端中间数据, 格式见
            // kGlobalDistanceFieldCompositeHeaderWordCount 的注释——不在这里就上传进 GPU
            // buffer, 真正的上传/拼装留给 GlobalDistanceFieldCompositePass 在自己的 pass
            // 里做(把多个层的数据合并成一份、一次 dispatch 处理完, 见该 pass 的注释)
            std::vector<uint32_t> pendingHeaderWords;
            std::vector<uint32_t> pendingPayloadWords;

            // updateCascadeLayer() 里这一层的候选剔除/间接索引表都重新算完、且
            // pendingHeaderWords 非空时置 true, 表示有新数据等着 GlobalDistanceFieldCompositePass
            // 消费; 合成 pass 认领后清掉(同时清空 pendingHeaderWords/pendingPayloadWords,
            // 避免下一帧这一层没脏时误用陈旧数据)
            bool needsComposite = false;
        };

        struct Cascade
        {
            double3 center{};
            float halfExtent = 0.0f;

            // 静态/动态物体分两层独立合成——静态层只在相机跨越重算阈值或者静态场景内容
            // 变化时才需要重算, 动态层每帧都可能因为动态物体移动而需要重算, 两者更新频率
            // 差异很大
            Layer staticLayer;
            Layer dynamicLayer;
        };

        using Cascades = std::array<Cascade, kGlobalDistanceFieldCascadeCount>;

        explicit GlobalDistanceField(RenderBackend& renderBackend) noexcept;

        ~GlobalDistanceField() noexcept;

        GlobalDistanceField(const GlobalDistanceField&) = delete;
        GlobalDistanceField& operator=(const GlobalDistanceField&) = delete;

        Cascades& getCascades() noexcept
        {
            return mCascades;
        }

        const Cascades& getCascades() const noexcept
        {
            return mCascades;
        }

        // 每帧调用一次, 不接 cmdList: 每一级 cascade 的两层里被标脏的那些重新剔除候选物体、
        // 分配 brick、把新的间接索引表内容排进待上传队列, 不脏的直接跳过——
        // 真正的 GPU 拷贝留给 prepare()。只接收候选剔选需要的 MeshRenderProxy 列表, 不认识
        // SceneProxy 这个类型本身——调用方(GlobalDistanceFieldCompositePass::addToRenderGraph())
        // 负责摘出这份数据传进来
        void update(std::span<MeshRenderProxy* const> meshRenderProxies, DistanceFieldBrickAtlas& brickAtlas);

        // 把 update() 排进队列的间接索引表更新真正拷贝进 GPU buffer——必须在
        // 消费这张 buffer 的 pass(GlobalDistanceFieldCompositePass)的 execute 回调之前
        // 执行
        void prepare(rhi::CommandList& cmdList);

        rhi::Buffer* getIndirectionBuffer() const noexcept
        {
            return mIndirectionBuffer.get();
        }

        // 为 true 时 getIndirectionBuffer() 可能指向还没合成写入的 atlas/Object Grid 槽位
        bool hasPendingComposite() const noexcept
        {
            for (const Cascade& cascade : mCascades)
            {
                if (cascade.staticLayer.needsComposite || cascade.dynamicLayer.needsComposite)
                    return true;
            }
            return false;
        }

        // 命中查询专用的 Object Grid(见上面 kGlobalDistanceFieldObjectGridCellsPerBrickAxis 的注释)——
        // 按 atlas 物理槽位号直接索引(GDFIndirection 查出来的 brickIndex), 不分 cascade/层,
        // 因为槽位号在整个共享 atlas 池里本来就是唯一的
        rhi::Buffer* getObjectGridBuffer() const noexcept
        {
            return mObjectGridBuffer.get();
        }

        // 某一级 cascade 的某一层在 getIndirectionBuffer() 里的起始下标(单位: uint32 个数,
        // 不是字节)——静态层排在偶数位、动态层排在奇数位, 采样端(第 7 步)要按同一个公式换算
        static uint32_t getLayerIndirectionOffset(int32_t cascadeIndex, bool isDynamicLayer) noexcept
        {
            const int32_t layerGlobalIndex = cascadeIndex * 2 + (isDynamicLayer ? 1 : 0);
            return static_cast<uint32_t>(layerGlobalIndex) * static_cast<uint32_t>(kGlobalDistanceFieldCellsPerCascade);
        }

    private:
        // update() 对每一级 cascade 的两层各调一次——拆成成员函数纯粹是为了不把 update()
        // 写成一个巨大的函数体, indirectionOffset 由调用方通过 getLayerIndirectionOffset()
        // 算好传入
        // mergeStaticLayer 只在处理动态层时为 true——动态层最终存的是"静态 ∪ 动态"的合并
        // 结果(见 GlobalDistanceFieldComposite.slang 顶部注释), 所以哪怕这个格子没有任何
        // 动态候选, 只要 cascade.staticLayer.indirectionCPU 里这个格子有 brick, 也要照样
        // 分配槽位、生成一条候选数为 0 的记录, 让合成阶段跑一次纯 merge 直通; 不这样做的话
        // 静态层的数据永远进不了动态层, 而查询端(第七步)按设计只读动态层
        void updateCascadeLayer(
            std::span<MeshRenderProxy* const> meshRenderProxies,
            DistanceFieldBrickAtlas& brickAtlas,
            const Cascade& cascade,
            Layer& layer,
            Mobility mobility,
            uint32_t indirectionOffset,
            bool mergeStaticLayer
        );

        // 失败返回 false(staging 预算不够), 调用方(updateCascadeLayer)决定要不要回滚这次
        // 尝试——懒创建 mIndirectionBuffer(第一次真的要排队上传数据时才建), 布局转换不依赖
        // 是否有待上传数据。成功时把 staging 分配追加进 mPendingIndirectionUploads, 真正的
        // copyBuffer 留给 prepare()
        bool queueLayerIndirectionUpload(uint32_t offset, std::span<const uint32_t> data);

        void ensureObjectGridBuffer(uint32_t atlasSlotCapacity);

    private:
        RenderBackend& mRenderBackend;
        Cascades mCascades;

        TRef<rhi::Buffer> mIndirectionBuffer;
        bool mIndirectionBufferUploaded = false; // 布局是否已经转过 Shader_Storage_Read

        struct PendingIndirectionUpload
        {
            uint32_t offset; // mIndirectionBuffer 里的目的地偏移, 单位是 uint32 个数
            StagingAllocation staging;
            uint32_t wordCount;
        };
        std::vector<PendingIndirectionUpload> mPendingIndirectionUploads;

        TRef<rhi::Buffer> mObjectGridBuffer;
    };
}
