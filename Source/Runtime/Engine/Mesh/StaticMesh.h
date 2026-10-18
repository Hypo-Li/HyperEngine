#pragma once
#include "Mesh.h"
#include <optional>
#include <span>

#include <Core/Common/Hash.h>
#include <RenderBase/MeshShapeTypes.h>
#include <RenderBase/ProxyHandle.h>

namespace hyp
{
    // 形状固定不变(不参与骨骼蒙皮/顶点动画)才成立的距离场/Mesh Card 烘焙数据和对应的
    // GPU 几何代理创建, 都只属于 StaticMesh
    class StaticMesh : public Mesh
    {
        REFLECT_CLASS(StaticMesh)
    public:
        StaticMesh() = default;

        ~StaticMesh() noexcept;

        // 网格自己的距离场密度倍率——是一个作者设定(跟 minScreenPercentage 一样是持久化
        // 属性, 不是烘焙出来的派生数据), 独立于 mDistanceFieldData 是否已经生成。默认 1.0
        // 表示用 MeshUtility 那边的全局默认密度; 个别需要更精细(比如带薄壁细节的道具)
        // 或者可以更粗糙(比如超大的简单几何体)的网格可以单独调整这个倍率, 不需要为它们
        // 手动填一个绝对体素尺寸——这样以后调整全局默认密度时, 这些网格的相对倍率关系
        // 还能保持, 不会因为它们被填过绝对值就跟不上全局调整
        float getDistanceFieldResolutionScale() const noexcept
        {
            return mDistanceFieldResolutionScale;
        }

        void setDistanceFieldResolutionScale(float scale) noexcept
        {
            assert(scale > 0.0f);
            mDistanceFieldResolutionScale = scale;
        }

        // denseDistances 是体素角点采样, size() 必须等于 (resolution.x+1)*(resolution.y+1)*(resolution.z+1), 且已经是
        // 经过窄带裁剪的值(窄带外的体素统一等于 +narrowBandDistance)——不做单独的 Bake
        // 逻辑, 调用方(MeshUtility)只管把稠密网格算出来传进来, 分块/稀疏化压缩+量化编码是这个
        // 函数内部的事: 按 brick 分块(分块常量定义在 RenderBase/DistanceFieldBrickAtlas.h,
        // Mesh 和渲染端共用同一份, 见 kDistanceFieldBrickSize/kDistanceFieldUniqueBrickSize), 一整块
        // 体素值都等于 narrowBandDistance
        // (说明整块都在窄带外, 没有表面细节)的块直接丢弃、只在 indirection 表里标记
        // "空", 不落盘；只有真正跨越窄带边界、包含有效距离信息的块才会被打包存进
        // brickData。这样存储量只跟"表面附近有多少体素"成正比, 不跟包围盒总体积成正比
        void setDistanceFieldData(int3 resolution, float3 boundsMin, float3 boundsMax, float narrowBandDistance, std::vector<float> denseDistances);

        // mDistanceFieldData 是 Cook_Only 属性, 编辑期资产从不落盘——已有数据直接返回;没有时
        // 先查本地 DDC 缓存(key = LOD0 几何内容哈希 + 烘焙参数), 命中就直接解包, 未命中才
        // 通过 IMeshUtility(见 Engine/Mesh/MeshUtility.h)现烘并写回缓存。只应在真正需要
        // MDF 的时候调用(比如场景开启 Lumen 时), 不是每次网格被渲染都调——烘焙本身是
        // 一次同步的、可能不便宜的 CPU 操作(建 BVH + 逐体素求交)
        void ensureDistanceFieldData();

        bool hasDistanceFieldData() const noexcept
        {
            return mDistanceFieldData.has_value();
        }

        int3 getDistanceFieldResolution() const noexcept
        {
            assert(mDistanceFieldData);
            return mDistanceFieldData->resolution;
        }

        const Boxf& getDistanceFieldBounds() const noexcept
        {
            assert(mDistanceFieldData);
            return mDistanceFieldData->bounds;
        }

        float getDistanceFieldNarrowBandDistance() const noexcept
        {
            assert(mDistanceFieldData);
            return mDistanceFieldData->narrowBandDistance;
        }

        // 目前没有任何调用方需要读回 brickData/brickIndirection 本身——真正需要读原始体素
        // 值的消费方(比如 MeshShapeProxy)是 StaticMesh 自己的成员函数(createMeshShapeProxy),
        // 已经有权限直接摸 mDistanceFieldData, 照抄 createGeometryProxy() 对 lod.vertexData
        // 的做法(用完就地 load/unload, 不经过任何公开包装)就够了, 不需要为此专门开一个
        // 公开的 CPU 端读接口
        // cards 是 MeshCard 的完整数组(局部空间), meshCardsBounds 是生成时用的(padding过
        // 的)网格包围盒, mostlyTwoSided 是生成时判定的双面材质占比结果, 不影响运行时逻辑,
        // 纯粹是"为什么这个网格只生成了这么少张卡"的诊断信息
        void setCardData(Boxf meshCardsBounds, bool mostlyTwoSided, std::vector<MeshCard> cards);

        // 通过 IMeshUtility(见 Engine/Mesh/MeshUtility.h)现烘并调用 setCardData() 写回——
        // 只应在导入时调用一次, 不像 ensureDistanceFieldData() 那样懒加载/查缓存(数据量小,
        // 不值得为它引入 DDC, 见 hasCardData() 的注释), 每次调用都会重新烘焙并覆盖已有数据
        void bakeCardData();

        // 数据量小(几十张卡顶天), 跟 Description/SampleState 这类小属性一样直接存, 不用
        // Cook_Only/DDC 那套——"没生成过"和"生成出来是空 vector"合并成同一个状态, 不需要
        // 用 optional 区分, 消费方(MeshShapeProxy 等)反正对这两种情况的处理是一样的
        bool hasCardData() const noexcept
        {
            return !mCardData.cards.empty();
        }

        const Boxf& getCardBounds() const noexcept
        {
            assert(hasCardData());
            return mCardData.meshCardsBounds;
        }

        bool isCardDataMostlyTwoSided() const noexcept
        {
            assert(hasCardData());
            return mCardData.mostlyTwoSided;
        }

        uint32_t getCardCount() const noexcept
        {
            return static_cast<uint32_t>(mCardData.cards.size());
        }

        // MeshShapeProxy 是这个 StaticMesh 的粗粒度形状代理(距离场 + Mesh Card 的 GPU 版本)——
        // 资产级别, 不按 LOD 建(距离场/Card 本来就是全 LOD 共享的一份, 见上面 mDistanceFieldData/
        // mCardData 的注释), 跟 Mesh::getOrCreateGeometryProxy() 是同一套"懒创建、
        // 句柄自己持有"的模式, 只是不需要 lodLevel 参数。要求调用前
        // hasDistanceFieldData()/hasCardData() 至少有一个为真, 否则创建出来的
        // proxy 没有任何数据
        MeshShapeProxyHandle getOrCreateMeshShapeProxy();

    protected:
        void createMeshShapeProxy(MeshShapeProxyHandle handle);

        void destroyMeshShapeProxy(MeshShapeProxyHandle handle) noexcept;

    private:
        // key 混入 LOD0 几何内容哈希 + 全部烘焙参数(含 mDistanceFieldResolutionScale)+ 存储
        // 格式版本号——几何、参数、存储格式任何一项变化都会让 key 变, 天然表现为"缓存
        // 未命中, 重新烘焙", 不需要显式失效逻辑
        // 非 const: 需要临时 load/unload LOD0 的 BulkData 才能读到字节内容参与哈希
        uint128_t computeDistanceFieldCacheKey();

        // 供 DDC 存取用: 打包/解包的是 mDistanceFieldData 已经稀疏化压缩+量化编码过的 brick
        // 表示, 不是稠密网格——避免每次缓存命中都要重新跑一遍 setDistanceFieldData() 的
        // 分块压缩。非 const: 同样需要临时 load/unload brickData
        std::vector<uint8_t> packDistanceFieldData();

        void unpackDistanceFieldData(std::span<const uint8_t> blob);

    private:
        float mDistanceFieldResolutionScale = 1.0f;

        // 分块参数(brick 边长/有效覆盖范围/无效哨兵值)不在这里定义——它们是 CPU(这里的
        // 打包/稀疏化)和 GPU(RenderBase::MeshShapeProxy 的间接索引重写、DistanceFieldBrickAtlas
        // 的槽位形状)共用的同一份存储格式常量, 唯一定义在 RenderBase/DistanceFieldBrickAtlas.h
        // 里(kDistanceFieldBrickSize/kDistanceFieldUniqueBrickSize/kInvalidDistanceFieldBrickIndex), 数值改变
        // 会让已经存盘的资产失效(需要重新烘焙)。这几个常量只在 StaticMesh.cpp 的实现里用到,
        // 不出现在这个头文件的任何声明中, 所以这里不 include 那个头, 只在 .cpp 里引用。
        //
        // 分块布局: kDistanceFieldBrickSize(8)是每块实际存储/采样的体素数, kDistanceFieldUniqueBrickSize
        // (7)才是这块真正"独有"、不和别的块重复的覆盖范围——块与块之间在网格坐标上按 7
        // 为步长排列(不是 8), 也就是说每块的最后一层体素, 在网格坐标上跟下一块的第一层
        // 体素是同一个位置, 会被写入两份(一份在这块末尾, 一份在下一块开头)。这层重叠是
        // 留给三线性插值用的: 落在某块覆盖范围内的任意一点, 只需要读这一块自己的 8^3
        // 数据就能做完整的三线性插值, 不需要同时跨两个物理上不保证相邻存放的块去取值
        // (brickData 是紧密排列的稀疏数组, 逻辑相邻的块在物理内存里几乎不可能真的相邻)。
        // 这个 7/8 的划分照抄自 UE DistanceField::UniqueDataBrickSize/BrickSize。

        // 网格预处理烘焙出来的距离场——是整个 StaticMesh 的形状代理, 不是某个 LOD 专属的数据
        // (相邻 LOD 轮廓几乎不变, 没必要、也不该每个 LOD 各存一份), 所以不放进 LOD
        // 结构体, 单独作为 StaticMesh 顶层的一份可选数据。
        //
        // 稀疏存储: 逻辑上仍然是 resolution.x*y*z 个体素的稠密网格, 但按每 kDistanceFieldUniqueBrickSize
        // (7)个体素一块划分(块内实际存 kDistanceFieldBrickSize=8 个, 含 1 层与相邻块重叠的边界,
        // 见上面的分块布局说明), 只有"块内至少一个体素不等于 narrowBandDistance"
        // (即块跨越了窄带边界, 包含有效表面信息)的块才会被打包进 brickData; 一整块都在
        // 窄带外的块直接省略, brickIndirection 里对应位置写 kInvalidDistanceFieldBrickIndex。采样时
        // 先查 brickIndirection 判断这个块是否存在, 不存在就直接返回 narrowBandDistance,
        // 存在才需要去 brickData 里取真正的体素值
        struct DistanceFieldData
        {
            int3 resolution{};
            Boxf bounds;
            float narrowBandDistance = 0.0f;
            int3 brickGridResolution{}; // ceil(resolution / kDistanceFieldUniqueBrickSize), 每维至少 1
            std::vector<uint32_t> brickIndirection; // brickGridResolution.x*y*z 个元素, 存的是该块在 brickData 里的起始块序号, 或者 kInvalidDistanceFieldBrickIndex
            BulkData brickData; // 紧密排列的有效块, 每块 kDistanceFieldBrickSize^3(含 1 层重叠边界)个 uint8(R8_UNORM 量化编码, [-narrowBandDistance, +narrowBandDistance] 线性映射到 [0,255];局部体素坐标: x 变化最快、然后 y、然后 z)
        };

        std::optional<DistanceFieldData> mDistanceFieldData;

        // 网格预处理生成的 Mesh Card 数据——跟 DistanceFieldData 同样的理由(整个 StaticMesh 的
        // 形状代理, 不是某个 LOD 专属), 不放进 LOD 结构体。数据量小(通常几张到几十张卡),
        // 不需要 BulkData 那套按需加卸载, 直接当普通反射属性存
        struct CardData
        {
            Boxf meshCardsBounds;
            bool mostlyTwoSided = false;
            std::vector<MeshCard> cards;
        };

        CardData mCardData;

        MeshShapeProxyHandle mMeshShapeProxy;
    };

    template <> Type* Reflection::createType<StaticMesh::DistanceFieldData>();

    template <> Type* Reflection::createType<StaticMesh::CardData>();

    template <> Type* Reflection::createType<StaticMesh>();
}
