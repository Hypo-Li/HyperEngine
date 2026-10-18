import Constants;

// 每个分区(PARTITION_SIZE 个元素)统计当前 pass 的 8 bit radix 的局部直方图,
// 累加进全局直方图,为 Spine 阶段的前缀和做准备
//
// 绑定到 group 0(而不是其它 pass 通用的 group 2):这个 shader 完全用不到全局的 group0(GlobalData)/
// group1(View),C++ 侧给它用的是只含自己这一个 BindGroupLayout 的精简 PipelineLayout
[[vk::binding(0, 0)]]
StructuredBuffer<uint> ElementCounts;

[[vk::binding(1, 0)]]
RWStructuredBuffer<Atomic<uint>> GlobalHistogram;

[[vk::binding(2, 0)]]
RWStructuredBuffer<uint> PartitionHistogram;

[[vk::binding(3, 0)]]
StructuredBuffer<uint> Keys;

[[vk::push_constant]]
uint kPass;

groupshared Atomic<uint> gLocalHistogram[RADIX];

[shader("compute")]
[numthreads(WORKGROUP_SIZE, 1, 1)]
void ComputeMain(uint3 groupThreadID : SV_GroupThreadID, uint3 groupId : SV_GroupID)
{
    // ElementCounts 借用的是 DrawIndirectCommand 整个 buffer,instanceCount 是它的第 2 个 uint32 字段
    uint elementCount = ElementCounts[1];

    uint index = groupThreadID.x;
    uint partitionIndex = groupId.x;
    uint partitionStart = partitionIndex * PARTITION_SIZE;

    // C++ 侧 dispatch 的分区数按场景总 splat 数算(CPU 提前已知),会比这里读到的实际可见数
    // (culling 之后才在 GPU 上算出来的 elementCount)大很多,多出来的都是"幽灵分区"——整个分区
    // 都落在可见范围外。如果不提前退出,幽灵分区会把 PARTITION_SIZE 个哨兵值(radix 255)错误地
    // 计入 GlobalHistogram,污染其它分区对 radix 255 的 dstOffset 计算,导致部分真实元素被挤出
    // 有效输出范围;整个分区都是同一批数据,uniform across 该 workgroup,提前 return 不会导致
    // barrier 死锁
    if (partitionStart >= elementCount)
    {
        return;
    }

    if (index < RADIX)
    {
        gLocalHistogram[index].store(0);
    }
    GroupMemoryBarrierWithGroupSync();

    [ForceUnroll]
    for (int i = 0; i < PARTITION_DIVISION; ++i)
    {
        uint keyIndex = partitionStart + WORKGROUP_SIZE * i + index;
        uint key = keyIndex < elementCount ? Keys[keyIndex] : 0xffffffff;
        uint radix = bitfieldExtract(key, 8 * kPass, 8);
        gLocalHistogram[radix].add(1);
    }
    GroupMemoryBarrierWithGroupSync();

    if (index < RADIX)
    {
        uint count = gLocalHistogram[index].load();
        // 分区自己的直方图,Spine 阶段要用来算分区间偏移
        PartitionHistogram[RADIX * partitionIndex + index] = count;
        // 累加进全局直方图,Spine 阶段用来算 radix 间偏移
        GlobalHistogram[RADIX * kPass + index].add(count);
    }
}
