import Constants;

// 把 Upsweep 阶段产出的每分区直方图(PartitionHistogram)做前缀和,
// 变成"这个分区、这个 radix 的元素该往输出的哪个偏移写";
// 同时把 GlobalHistogram(每个 radix 的总数)也做一次前缀和,得到 radix 间的基准偏移
//
// 绑定到 group 0(而不是其它 pass 通用的 group 2):这个 shader 完全用不到全局的 group0(GlobalData)/
// group1(SceneAndView),C++ 侧给它用的是只含自己这一个 BindGroupLayout 的精简 PipelineLayout
[[vk::binding(0, 0)]]
StructuredBuffer<uint> ElementCounts;

[[vk::binding(1, 0)]]
RWStructuredBuffer<uint> GlobalHistogram;

[[vk::binding(2, 0)]]
RWStructuredBuffer<uint> PartitionHistogram;

[[vk::push_constant]]
uint kPass;

groupshared uint gReduction;
groupshared uint gIntermediate[MAX_SUBGROUP_SIZE];

// 按 (RADIX, 1, 1) 派发,所以 groupId.x 就是当前处理的 radix 数字
[shader("compute")]
[numthreads(WORKGROUP_SIZE, 1, 1)]
void ComputeMain(uint3 groupId : SV_GroupID, uint groupIndex : SV_GroupIndex)
{
    // ElementCounts 借用的是 DrawIndirectCommand 整个 buffer,instanceCount 是它的第 2 个 uint32 字段
    uint elementCount = ElementCounts[1];

    uint laneIndex = WaveGetLaneIndex();
    uint laneCount = WaveGetLaneCount();
    uint waveIndex = groupIndex / laneCount;
    uint waveCount = WORKGROUP_SIZE / laneCount;
    uint index = waveIndex * laneCount + laneIndex;

    uint radix = groupId.x;

    uint partitionCount = (elementCount + PARTITION_SIZE - 1) / PARTITION_SIZE;

    if (index == 0)
    {
        gReduction = 0;
    }
    GroupMemoryBarrierWithGroupSync();

    for (uint i = 0; WORKGROUP_SIZE * i < partitionCount; ++i)
    {
        uint partitionIndex = WORKGROUP_SIZE * i + index;
        uint value = partitionIndex < partitionCount ? PartitionHistogram[RADIX * partitionIndex + radix] : 0;
        uint excl = WavePrefixSum(value) + gReduction;
        uint sum = WaveActiveSum(value);

        if (WaveIsFirstLane())
        {
            gIntermediate[waveIndex] = sum;
        }
        GroupMemoryBarrierWithGroupSync();

        // gIntermediate 里只有前 waveCount 个槽位有效,而 waveCount 通常远小于 laneCount(比如
        // 256/32=8 个 wave,一个 32 lane 的 subgroup 里就只有 lane 0~7 满足 index<waveCount)——
        // 这个条件在同一个 subgroup 内部并不一致,WGSL 的 subgroup 内建函数要求必须在
        // subgroup-uniform 控制流下调用,分支内调用会被 Tint 拒绝编译。
        // 参考 webgpu_radix_sort(jaesung-cs)原生 WGSL 实现的写法:不在分支里调用 wave 内建函数,
        // 而是让整个 subgroup 都无条件参与,越界的 lane 代入安全占位值 0(不影响真正参与者的
        // 求和/前缀和结果),分支只用来决定要不要写回——这样不用退化成单 lane 顺序扫描。
        // gIntermediate 只有 MAX_SUBGROUP_SIZE(128)大小,index 最大可达 WORKGROUP_SIZE-1(255),
        // 用取模保证读下标总是在数组范围内,读到的值只在 index<waveCount 时才会被用到
        {
            uint v = index < waveCount ? gIntermediate[index % MAX_SUBGROUP_SIZE] : 0;
            uint innerSum = WaveActiveSum(v);
            uint innerExcl = WavePrefixSum(v);
            if (index < waveCount)
            {
                gIntermediate[index] = innerExcl;
            }
            if (index == 0)
            {
                gReduction += innerSum;
            }
        }
        GroupMemoryBarrierWithGroupSync();

        if (partitionIndex < partitionCount)
        {
            excl += gIntermediate[waveIndex];
            PartitionHistogram[RADIX * partitionIndex + radix] = excl;
        }
        GroupMemoryBarrierWithGroupSync();
    }

    if (radix == 0)
    {
        // 一个 workgroup(radix == 0 那个)顺带把 GlobalHistogram 也做了前缀和
        // 注意:RADIX 和 WORKGROUP_SIZE 在 Constants.slang 中都固定为 256,index 的取值范围
        // 恒等于 [0, WORKGROUP_SIZE) == [0, RADIX),所以这里不再包一层 `if (index < RADIX)`——
        // 那层判断永远为真,但会让 WGSL 后端的 uniformity 分析把下面的 GroupMemoryBarrierWithGroupSync()
        // 误判成"可能只有部分线程执行的 barrier"从而编译失败(WaveGetLaneIndex/WaveGetLaneCount 派生值
        // 经过 private 变量传递后,分析器无法证明该条件对整个 workgroup 恒真)。
        uint value = GlobalHistogram[RADIX * kPass + index];
        uint excl = WavePrefixSum(value);
        uint sum = WaveActiveSum(value);

        if (WaveIsFirstLane())
        {
            gIntermediate[waveIndex] = sum;
        }
        GroupMemoryBarrierWithGroupSync();

        // 原因和写法同上面第一处:RADIX/laneCount(==waveCount)个有效槽位,整个 subgroup 无条件
        // 参与、越界读代入 0,只在写回时判断 index<waveCount
        {
            uint v = index < waveCount ? gIntermediate[index % MAX_SUBGROUP_SIZE] : 0;
            uint innerExcl = WavePrefixSum(v);
            if (index < waveCount)
            {
                gIntermediate[index] = innerExcl;
            }
        }
        GroupMemoryBarrierWithGroupSync();

        excl += gIntermediate[waveIndex];
        GlobalHistogram[RADIX * kPass + index] = excl;
    }
}
