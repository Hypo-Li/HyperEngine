// 参考 https://github.com/jaesung-cs/vulkan_radix_sort 的 reduce-then-scan GPU 基数排序,
// 256 基数(8 bit/pass),32 位 key 需要 4 个 pass(Upsweep -> Spine -> Downsweep)
static const uint RADIX = 256;
static const uint WORKGROUP_SIZE = 256;
static const uint PARTITION_DIVISION = 8;
static const uint PARTITION_SIZE = PARTITION_DIVISION * WORKGROUP_SIZE;
static const uint MAX_SUBGROUP_SIZE = 128;
