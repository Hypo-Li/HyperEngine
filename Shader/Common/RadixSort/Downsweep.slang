import Constants;

// 实际的重排(scatter)阶段:每个线程组处理一个分区,按当前 pass 的 8 bit radix
// 把 key(以及绑定的 value)写到 Spine 阶段算好的目标偏移上。
// key+value 一起排(不是纯 key 排序)——GSplat 用它按距离(key)排 splat 索引(value)。
//
// 本文件按 upstream vulkan_radix_sort 官方源码(downsweep_key_value.slang)逐行对齐移植,
// 只改了 buffer 绑定语法(改用本引擎的 [[vk::binding(N, 2)]] + ElementCounts[1] 那套约定)和
// atomic 写法(groupshared 数组用 Atomic<uint> 包一层,其余算法结构、变量名、每一步的顺序
// 都和官方源码保持一致)。
//
// === 排查记录(重要,后续接手请先读完再动手)===
// 早期照着自己理解重写的版本,稳定复现"radix=0 桶被放大约 4 倍"的 bug,且被反复验证只出在
// radix=0(逻辑上第一个桶),和物理内存布局、原子/非原子写法、workgroup size 等都无关(完整排查
// 过程见 git 历史/对话记录)。根因位于 waveMatch8 风格的位比对逻辑:找 wave 内和自己 radix 相同
// 的 lane,需要逐位对 8 bit radix 做 WaveActiveBallot、按位不命中就取反再 AND。如果种子
// mask 是硬编码的全 1 常量,而不是"当前 wave 里真实活跃 lane"的 ballot,那么只要某个 radix 的
// 8 个 bit 全是 0(唯一符合条件的就是 radix=0 本身,因为它 8 轮全部走"取反"分支),取反时可能把
// 超出实际 lane 数的无效位翻转成 1,而且永远没有机会被"直接用原生 ballot"的分支洗回 0(其它
// radix 只要有 1 位是 1,那一轮就会用未取反的原生 ballot 把无效位洗白)。
// 本文件用官方写法从根上避免了这个问题:第 51 行 `mask = WaveActiveBallot(true)` 用真实活跃
// lane 的 ballot 做种子,后面 8 轮循环永远只做 `&=`(只会清零位,不会把 0 变回 1)——无效位从
// 源头就被钉死为 0,不需要事后再补掩码。已通过对比 ThirdParty/vk_radix_sort/vk_radix_sort.h 里
// 内嵌的 downsweep_key_value_slang 预编译 SPIR-V(独立测试程序验证过在本机 GPU 上 0 错误)反汇编,
// 确认变量名(digit/mask)和 mask 的初始化指令(OpGroupNonUniformBallot ... %true)完全对应,
// 说明这份 downsweep.slang 就是那份验证过正确的二进制所对应的源码。

// 绑定到 group 0(而不是其它 pass 通用的 group 2):Downsweep 自己就需要 7 个 storage buffer,
// 如果再套上 group 0(全局数据)/group 1(场景和视图)的 PipelineLayout,storage buffer 总数会
// 超过 WebGPU 单 stage 8 个的限制(且这两组 Downsweep 完全用不到)。C++ 侧给 Downsweep 用了一个
// 只含它自己这一个 BindGroupLayout 的精简 PipelineLayout,所以这里从 0 开始编号
[[vk::binding(0, 0)]]
StructuredBuffer<uint> ElementCounts;

[[vk::binding(1, 0)]]
StructuredBuffer<uint> GlobalHistogram;

[[vk::binding(2, 0)]]
StructuredBuffer<uint> PartitionHistogram;

[[vk::binding(3, 0)]]
StructuredBuffer<uint> KeysIn;

[[vk::binding(4, 0)]]
RWStructuredBuffer<uint> KeysOut;

[[vk::binding(5, 0)]]
StructuredBuffer<uint> ValuesIn;

[[vk::binding(6, 0)]]
RWStructuredBuffer<uint> ValuesOut;

[[vk::push_constant]]
uint kPass;

// PARTITION_DIVISION=8 时,RADIX(256) * waveCount(WORKGROUP_SIZE/32) 恰好等于 PARTITION_SIZE
// (= PARTITION_DIVISION * WORKGROUP_SIZE),所以这一块 groupshared 内存可以先当"按 (radix, wave)
// 分桶的直方图"用,histogram 阶段结束后原地复用(alias)成分区内 key/value 重排的暂存区,不用
// 额外多开一块 shared memory
groupshared Atomic<uint> gLocalHistogram[PARTITION_SIZE];
groupshared uint gLocalHistogramSum[RADIX];

// 返回一个 mask:比 id 小的所有 lane 对应的位是 1,其余(含 id 自己和更大的 lane)是 0。
// 用有符号右移的符号扩展技巧做成无分支版本,支持最多 128 个 lane(uint4 的 4 个分量)
uint4 GetExclusiveWaveMask(uint id)
{
    uint shift = (1u << bitfieldExtract(id, 0, 5)) - 1u; // (1 << (id % 32)) - 1
    int x = int(id) >> 5;
    return uint4(
        (shift & uint((-1 - x) >> 31)) | uint((0 - x) >> 31),
        (shift & uint((0 - x) >> 31)) | uint((1 - x) >> 31),
        (shift & uint((1 - x) >> 31)) | uint((2 - x) >> 31),
        (shift & uint((2 - x) >> 31)) | uint((3 - x) >> 31)
    );
}

uint GetBitCount(uint4 value)
{
    uint4 result = countbits(value);
    return result.x + result.y + result.z + result.w;
}

[shader("compute")]
[numthreads(WORKGROUP_SIZE, 1, 1)]
void ComputeMain(uint3 groupThreadID : SV_GroupThreadID, uint3 groupId : SV_GroupID, uint groupIndex : SV_GroupIndex)
{
    // ElementCounts 借用的是 DrawIndirectCommand 整个 buffer,instanceCount 是它的第 2 个 uint32 字段
    uint elementCount = ElementCounts[1];

    uint laneIndex = WaveGetLaneIndex();
    uint laneCount = WaveGetLaneCount();
    uint waveIndex = groupIndex / laneCount;
    uint waveCount = WORKGROUP_SIZE / laneCount;
    uint index = waveIndex * laneCount + laneIndex;

    uint4 waveMask = GetExclusiveWaveMask(laneIndex);

    uint partitionIndex = groupId.x;
    uint partitionStart = partitionIndex * PARTITION_SIZE;

    // 原因同 Upsweep.slang:见该文件里的详细注释
    if (partitionStart >= elementCount)
    {
        return;
    }

    if (index < RADIX)
    {
        for (uint i = 0; i < waveCount; ++i)
        {
            gLocalHistogram[waveCount * index + i].store(0);
        }
    }
    GroupMemoryBarrierWithGroupSync();

    // load from global memory, local histogram and offset
    uint localKeys[PARTITION_DIVISION];
    uint localRadix[PARTITION_DIVISION];
    uint localOffsets[PARTITION_DIVISION];
    uint waveHistogram[PARTITION_DIVISION];
    uint localValues[PARTITION_DIVISION];

    [ForceUnroll]
    for (int i = 0; i < PARTITION_DIVISION; ++i)
    {
        uint keyIndex = partitionStart + (PARTITION_DIVISION * laneCount) * waveIndex + i * laneCount + laneIndex;
        uint key = keyIndex < elementCount ? KeysIn[keyIndex] : 0xffffffff;
        localKeys[i] = key;
        localValues[i] = keyIndex < elementCount ? ValuesIn[keyIndex] : 0;

        uint radix = bitfieldExtract(key, kPass * 8, 8);
        localRadix[i] = radix;

        // mask per digit:wave 内所有和我 radix 完全相同的 lane 的 ballot mask。种子是真实活跃
        // lane 的 ballot(而不是硬编码全 1),后面每一轮只做 AND——无效 lane 位从一开始就是 0,
        // 不管中间哪一轮取反出什么,AND 之后永远还是 0,不需要事后再补掩码
        uint4 mask = WaveActiveBallot(true);
        [ForceUnroll]
        for (int j = 0; j < 8; ++j)
        {
            uint digit = (radix >> j) & 1u;
            uint4 ballot = WaveActiveBallot(digit == 1u);
            // digit - 1 是 0 或 0xffffffff,异或实现"命中用原始 ballot、不命中取反"
            mask &= uint4(digit - 1u) ^ ballot;
        }

        // wave level offset for radix
        uint waveOffset = GetBitCount(waveMask & mask);
        uint radixCount = GetBitCount(mask);

        // elect a representative per radix, add to histogram
        if (waveOffset == 0)
        {
            gLocalHistogram[waveCount * radix + waveIndex].add(radixCount);
            waveHistogram[i] = radixCount;
        }
        else
        {
            waveHistogram[i] = 0;
        }

        localOffsets[i] = waveOffset;
    }
    GroupMemoryBarrierWithGroupSync();

    // local histogram reduce RADIX * waveCount(256 * 8 = 2048)。原来是 `for (i=index; i<RADIX*waveCount;
    // i+=WORKGROUP_SIZE)`——虽然 RADIX==WORKGROUP_SIZE 能保证每个线程的循环次数其实都一样(都是
    // waveCount 轮),但循环边界表达式本身经过 waveCount(laneCount 派生)污染,Tint 没法证明这一点,
    // 循环体里的 subgroup 内建函数就会被当成"可能不是所有 lane 都会执行同样次数"而拒绝编译。
    // gLocalHistogram 数组本来就是按 PARTITION_DIVISION(=8)份 WORKGROUP_SIZE 大小分配的(见上面
    // 数组声明处的注释,要求 waveCount<=PARTITION_DIVISION 数组才不越界),所以固定跑
    // PARTITION_DIVISION 轮是这个文件本来就有的上限,不是新引入的假设:每一轮所有线程都无条件调用
    // WaveActiveSum/WavePrefixSum(满足 subgroup-uniform),超出实际 waveCount 的那几轮只读一个安全
    // 的占位下标、不做任何写回,不影响正确性。
    [ForceUnroll]
    for (uint row = 0; row < PARTITION_DIVISION; ++row)
    {
        bool valid = row < waveCount;
        uint i = valid ? (index + row * WORKGROUP_SIZE) : index;
        uint v = gLocalHistogram[i].load();
        uint sum = WaveActiveSum(v);
        uint excl = WavePrefixSum(v);
        if (valid)
        {
            gLocalHistogram[i].store(excl);
            if (laneIndex == 0)
            {
                gLocalHistogramSum[i / laneCount] = sum;
            }
        }
    }
    GroupMemoryBarrierWithGroupSync();

    // local histogram reduce 第二级(按 laneCount 个一组继续往上归约)。
    // 早期版本这里在 `if (index < intermediateOffset0)` 分支里调用 WaveActiveSum/WavePrefixSum——
    // intermediateOffset0 不保证是 laneCount 的整数倍,同一个 subgroup 内部这个条件不一定一致,
    // WGSL 要求 subgroup 内建函数必须在 subgroup-uniform 控制流下调用,分支内调用被 Tint 拒绝,
    // 当时改成了单 lane 顺序扫描规避,但牺牲了这一级的并行度。
    // 参考 webgpu_radix_sort(jaesung-cs)原生 WGSL 实现的写法:不在分支里调用 wave 内建函数,而是
    // 让所有 lane 都无条件参与(越界的 lane 用安全占位值 0 代入,不影响真正参与者的前缀和结果),
    // 分支只用来决定"要不要写回"——这样既满足 subgroup-uniform,又不用退化成顺序扫描
    uint intermediateOffset0 = RADIX * waveCount / laneCount;
    {
        // gLocalHistogramSum 按 RADIX(=WORKGROUP_SIZE)大小分配,index 本身恒在 [0, RADIX) 范围内,
        // 这个读取始终安全;越界(index >= intermediateOffset0)的 lane 读到的值不会被用到
        uint v = index < intermediateOffset0 ? gLocalHistogramSum[index] : 0;
        uint sum = WaveActiveSum(v);
        uint excl = WavePrefixSum(v);
        if (index < intermediateOffset0)
        {
            gLocalHistogramSum[index] = excl;
        }
        if (index < intermediateOffset0 && laneIndex == 0)
        {
            gLocalHistogramSum[intermediateOffset0 + index / laneCount] = sum;
        }
    }
    GroupMemoryBarrierWithGroupSync();

    // local histogram reduce 第三级(通常只剩几个元素)。原因和写法同第二级
    uint intermediateSize1 = max(RADIX * waveCount / laneCount / laneCount, 1u);
    {
        uint readIndex = intermediateOffset0 + index;
        uint v = readIndex < RADIX ? gLocalHistogramSum[readIndex] : 0;
        uint excl = WavePrefixSum(v);
        if (index < intermediateSize1)
        {
            gLocalHistogramSum[intermediateOffset0 + index] = excl;
        }
    }
    GroupMemoryBarrierWithGroupSync();

    // local histogram add 第二级往回加
    if (index < intermediateOffset0)
    {
        gLocalHistogramSum[index] += gLocalHistogramSum[intermediateOffset0 + index / laneCount];
    }
    GroupMemoryBarrierWithGroupSync();

    // local histogram add 第一级往回加,此时 gLocalHistogram[slot] 变成"这个 (radix, wave) 槽位
    // 在整个分区范围内的独占前缀和"
    for (uint i = index; i < RADIX * waveCount; i += WORKGROUP_SIZE)
    {
        uint v = gLocalHistogram[i].load();
        gLocalHistogram[i].store(v + gLocalHistogramSum[i / laneCount]);
    }
    GroupMemoryBarrierWithGroupSync();

    // post-scan stage:localOffsets[i] 目前是"这一轮匹配组内的秩",加上 (radix, wave) 的独占前缀
    // 就是分区内的初步位置;随后用第二轮原子加(严格按 i 递增的顺序、barrier 隔开)把同一个
    // (radix, wave) 槽位里不同轮次(i)之间的相对顺序也确定下来,保证多趟基数排序所需的稳定性
    [ForceUnroll]
    for (int i = 0; i < PARTITION_DIVISION; ++i)
    {
        uint radix = localRadix[i];
        localOffsets[i] += gLocalHistogram[waveCount * radix + waveIndex].load();

        GroupMemoryBarrierWithGroupSync();
        if (waveHistogram[i] > 0)
        {
            gLocalHistogram[waveCount * radix + waveIndex].add(waveHistogram[i]);
        }
        GroupMemoryBarrierWithGroupSync();
    }

    // 两轮原子加之后,gLocalHistogram[waveCount*radix+wave] 是"radix 桶里、到这个 wave(含)为止"
    // 的分区内包含前缀和;同一个 radix 的 waveCount 个槽位在数组里是连续的一段,所以"我这个 radix
    // 块开始前一个槽位"(index-1)正好是上一个 radix 块的最后一个槽位,也就是"分区内所有 radix
    // 比我小的元素总数"——不需要再对 256 个 radix 单独做一次前缀和,顺带就拿到了
    if (index < RADIX)
    {
        uint v = index == 0 ? 0 : gLocalHistogram[waveCount * index - 1].load();
        gLocalHistogramSum[index] = GlobalHistogram[RADIX * kPass + index] + PartitionHistogram[RADIX * partitionIndex + index] - v;
    }
    GroupMemoryBarrierWithGroupSync();

    // 把 key 按局部偏移重新排列,让写回全局内存的地址接近连续,提升带宽利用率;
    // histogram 已经用完,gLocalHistogram 原地复用成分区内 key/value 重排的暂存区
    [ForceUnroll]
    for (int i = 0; i < PARTITION_DIVISION; ++i)
    {
        gLocalHistogram[localOffsets[i]].store(localKeys[i]);
    }
    GroupMemoryBarrierWithGroupSync();

    for (uint i = index; i < PARTITION_SIZE; i += WORKGROUP_SIZE)
    {
        uint key = gLocalHistogram[i].load();
        uint radix = bitfieldExtract(key, kPass * 8, 8);
        uint dstOffset = gLocalHistogramSum[radix] + i;
        if (dstOffset < elementCount)
        {
            KeysOut[dstOffset] = key;
        }
        // 复用 localKeys 记录每个 slot 最终写去的目标偏移,下面重排 value 时要用
        localKeys[i / WORKGROUP_SIZE] = dstOffset;
    }
    GroupMemoryBarrierWithGroupSync();

    [ForceUnroll]
    for (int i = 0; i < PARTITION_DIVISION; ++i)
    {
        gLocalHistogram[localOffsets[i]].store(localValues[i]);
    }
    GroupMemoryBarrierWithGroupSync();

    for (uint i = index; i < PARTITION_SIZE; i += WORKGROUP_SIZE)
    {
        uint value = gLocalHistogram[i].load();
        uint dstOffset = localKeys[i / WORKGROUP_SIZE];
        if (dstOffset < elementCount)
        {
            ValuesOut[dstOffset] = value;
        }
    }
}
