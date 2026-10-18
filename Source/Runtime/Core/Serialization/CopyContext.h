#pragma once
#include "../Object/Object.h"

#include <span>
#include <unordered_map>
#include <vector>

namespace hyp
{
    // Copier 一次拷贝操作内的会话状态: 维护"原对象 -> 副本"的身份映射。跟 SerializeContext 是
    // 同一类角色(遍历过程中的上下文), 但纯粹在内存里工作——不涉及 Archive/AssetManager/
    // BulkData, 也不需要跨文件的 Guid 去重, 所以结构比 SerializeContext 简单得多, 是一个
    // 独立的类, 不是它的变体。
    //
    // 用法分两趟:
    // 1. 扫描阶段(Copier::scan): 沿着 CopyPolicy::Deep 的 Object 引用递归发现需要复制的对象,
    //    对每一个调用一次 getOrCreateCopy() 提前分配好空副本(不填属性), 保证在第二趟开始之前,
    //    本次操作范围内所有对象的身份都已经确定。
    // 2. 填充阶段(Copier::populate): 对每个副本, 把原对象的属性值搬过来; 遇到 Object 引用时用
    //    findCopy() 查表决定是重定向到副本还是保留指向原对象——这个查询能拿到正确结果,
    //    前提就是扫描阶段已经把所有该分配的身份都分配完了。
    class CopyContext
    {
    public:
        // 已经登记过 original 就直接返回对应副本(isNew = false); 否则按 original 的实际
        // Class 分配一个新的空实例(属性未填充), 登记进映射后返回(isNew = true)。
        // newOwner 是这个副本在对象树里的归属(通常是"正在被填充的那个父对象的副本"),
        // 不是 original 自己的 owner。
        Object* getOrCreateCopy(Object* original, Object* newOwner, bool& isNew);

        // 登记一对不是本次 scan() 分配出来的映射(比如调用方自己持久化维护的、跨会话的身份
        // 映射表)——不写入 mAllocationOrder, 不会被 takeNewlyAllocated()/populate() 当成
        // 待填充的新副本重新处理一遍。
        void registerExternalCopy(Object* original, Object* copy);

        // 查 original 在本次操作里是否已经有对应副本, 没有则返回 nullptr——Shallow 引用靠这个
        // 结果决定该重定向还是保留原值, Deep 引用应该总能查到(扫描阶段已经保证)。
        Object* findCopy(Object* original) const;

        // 返回自上一次调用以来新分配的原对象(按分配顺序), 并把内部游标前移到末尾——
        // Copier::finalizeAll() 靠这个只处理自己还没处理过的那一段, 不会把之前已经处理过的
        // 对象重复处理一遍。
        std::span<Object* const> takeNewlyAllocated();

    private:
        std::unordered_map<Object*, TRef<Object>> mCopies;
        std::vector<Object*> mAllocationOrder;
        size_t mConsumedCount = 0;
    };
}
