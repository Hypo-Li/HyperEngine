#pragma once
#include "StagingAllocator.h"

#include <Core/Common/Refable.h>
#include <Core/Math/Vector.h>
#include <RHI/Buffer.h>
#include <RHI/Texture.h>
#include <RHI/CommandList.h>

#include <cstdint>
#include <span>
#include <vector>

namespace hyp
{
    class RenderBackend;

    // 稀疏体素 brick 的分块格式常量——CPU 端(Mesh 烘焙/打包)和 GPU 端(这个池子的槽位
    // 形状、MeshShapeProxy 的间接索引重写)都要用同一份, 只在这一处定义, 不允许各自重复
    // 定义同样的数值。
    //
    // kDistanceFieldBrickSize(8)是每块实际存储/采样的体素数, kDistanceFieldUniqueBrickSize(7)才是这块
    // 真正"独有"、不和别的块重复的覆盖范围——块与块之间在网格坐标上按 7 为步长排列
    // (不是 8), 每块的最后一层体素在网格坐标上跟下一块的第一层体素是同一个位置, 会被
    // 写入两份。这层重叠是留给三线性插值用的: 落在某块覆盖范围内的任意一点, 只需要读这
    // 一块自己的 8^3 数据就能做完整的三线性插值, 不需要同时跨两个物理上不保证相邻存放的
    // 块去取值。
    inline constexpr int32_t kDistanceFieldBrickSize = 8;
    inline constexpr size_t kDistanceFieldBrickVoxelCount = kDistanceFieldBrickSize * kDistanceFieldBrickSize * kDistanceFieldBrickSize;
    inline constexpr int32_t kDistanceFieldUniqueBrickSize = 7;

    // 间接索引表里代表"这个格子没有对应 brick"的哨兵值
    inline constexpr uint32_t kInvalidDistanceFieldBrickIndex = 0xFFFFFFFFu;

    // 跨网格共享的 brick 池——把所有网格烘焙出来的稀疏体素数据打包进同一张 3D 纹理,
    // 用固定大小的槽位(每个槽位正好放一个 brick)做分配, 避免每个网格各自开一张小纹理。
    // 名字点出存的是距离场体素, 但不绑定 Lumen——这个类现在有两个实例(per-mesh 局部
    // SDF 源数据, 见 RenderBackend::getMeshDistanceFieldBrickAtlas; 世界空间 GDF 合成输出,
    // 见 LumenScene::getGlobalDistanceFieldBrickAtlas), 两者的共同点只是"都是距离场体素 brick",
    // 别的系统需要同样的稀疏体素存储也能直接复用, 不是 GI/Lumen 专属的东西。
    //
    // 容量固定, 不支持运行时扩容——GPU 纹理没法原地扩容, 真要扩容得整个重新分配一张更大
    // 的纹理再把已有内容搬过去, 是一次代价不小的操作, 目前用不上就先不做; 分配失败(槽位
    // 不够)由调用方决定怎么降级。
    //
    // 纹理形状是尽量接近立方体的三维网格, 不是"宽高固定、纵深线性堆叠所有 brick"——3D
    // 纹理单轴尺寸有硬件上限(Vulkan/WebGPU 常见门槛在几百到 2048 体素量级, 而且对每一根轴
    // 都生效), 线性堆叠在容量稍大一点就会先撞到这个上限; 立方体形状让三个轴分摊增长,
    // 同样的硬件限制下能撑住的总容量大得多。
    class DistanceFieldBrickAtlas
    {
    public:
        // enableStorageWrite: 这张 atlas 是否需要被 compute shader 直接写(RWTexture/WTexture)——
        // 目前只有 hyp::LumenScene::mGlobalDistanceFieldBrickAtlas(GDF 合成 pass 的写入目标)需要,
        // per-mesh 那张共享池永远只走 queueBrickUploads() 的 CPU->GPU 拷贝上传, 不需要多开
        // 这个 usage bit
        DistanceFieldBrickAtlas(RenderBackend& renderBackend, uint32_t capacityInBricks, bool enableStorageWrite = false);

        ~DistanceFieldBrickAtlas();

        DistanceFieldBrickAtlas(const DistanceFieldBrickAtlas&) = delete;
        DistanceFieldBrickAtlas& operator=(const DistanceFieldBrickAtlas&) = delete;

        // 成功分配 brickCount 个槽位就返回它们的全局槽位号(不保证连续, 不代表 atlas 里
        // 相邻); 容量不够返回空 vector
        std::vector<uint32_t> allocate(uint32_t brickCount);

        void free(std::span<const uint32_t> slots);

        // 把 slots[i] 对应的体素数据(voxelData 里第 i*brickSize^3 个字节开始的一段, 每体素
        // 1 字节, R8_UNORM 量化编码——编码公式见 StaticMesh.cpp::encodeSignedDistance())
        // 排进待上传队列。要求 voxelData.size() == slots.size() * brickSize^3
        void queueBrickUploads(std::span<const uint32_t> slots, std::vector<uint8_t>&& voxelData);

        // 每帧调用一次: 第一次调用时把 atlas 纹理从 UNDEFINED 转成 Shader_Sampled(不依赖
        // 是否有待上传数据——消费方可能完全不走 queueBrickUploads() 这条路, 比如直接用
        // compute shader 写 atlas), 之后有待上传数据就 flush
        void prepare(rhi::CommandList& cmdList);

        // 槽位号 -> atlas 里这个 brick 的体素坐标原点(已经乘过 brickSize, 不是 brick
        // 网格坐标)
        int3 slotToAtlasVoxelOrigin(uint32_t slot) const noexcept;

        // atlas 每根轴排了多少个 brick(不是体素数)
        int3 getBrickGridExtent() const noexcept
        {
            return mBrickGridExtent;
        }

        rhi::Texture& getAtlasTexture() const noexcept
        {
            return *mAtlas;
        }

        rhi::TextureView& getAtlasTextureView() const noexcept
        {
            return *mAtlasView;
        }

        // atlas 纹理真实 GPU 布局是否已经从 None 转到 Shader_Sampled——调用方(比如
        // MeshSDFPass)在把这张纹理导入渲染图、声明它"当前处于 Shader_Sampled"之前,
        // 必须先用这个查一下。声明了 Shader_Sampled 但实际还是 None 会直接触发校验层报错
        // (真实图像布局对不上)
        bool isInitialized() const noexcept
        {
            return mInitialized;
        }

    private:
        void flushUploads(rhi::CommandList& cmdList);

    private:
        RenderBackend& mRenderBackend;
        uint32_t mCapacityInBricks;
        int3 mBrickGridExtent;

        TRef<rhi::Texture> mAtlas;
        TRef<rhi::TextureView> mAtlasView;
        bool mInitialized = false;

        std::vector<uint32_t> mFreeSlots;

        struct PendingBricksUpload
        {
            std::vector<uint32_t> slots;
            std::vector<uint8_t> voxelData;
        };
        std::vector<PendingBricksUpload> mPendingUploads;
    };
}
