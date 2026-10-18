#pragma once
#include "CopyContext.h"
#include "../Reflection/Reflection.h"

#include <functional>
#include <vector>

namespace hyp
{
    // 用反射的 CopyPolicy(Ignore/Shallow/Deep)驱动的对象图拷贝器。跟 Serializer 是同一类角色
    // (遍历 Class/Property/Special 元数据, 构造时绑定住一份上下文), 但服务的是完全不同的方向:
    // Serializer 面向"内存对象 <-> 外部字节流"的转换, Copier 面向"内存对象 -> 另一份独立的
    // 内存对象", 不涉及 Archive, 也不看 import/export 表, 所以是一个独立的类, 不是在 Serializer
    // 上加分支。
    //
    // 核心约定:
    // - CopyPolicy 只对声明类型里"是 Object 引用"的槽位有意义(TRef<T>/T* 本身, 或者容器/结构体
    //   内部嵌套的这类槽位); 不含任何 Object 引用的属性, 一次 Property::setValue() 用真实的
    //   C++ 赋值语义整体拷贝即可, 不需要关心 CopyPolicy。
    // - 一个属性的 CopyPolicy 统一套用到它声明类型里能找到的每一个 Object 引用槽位, 不管有
    //   几个、嵌套多深——std::map<Object*, TRef<Object>> 这种 key/value 都是 Object 的情况,
    //   两侧按同一个策略处理, 不需要也不允许分别指定; 这个假设由属性的作者在决定要不要给
    //   这个属性注册、注册成什么策略的时候就该想清楚, Copier 不做额外限制。
    // - Deep 引用在扫描阶段(scan)递归发现并提前分配好副本; Shallow 引用在填充阶段(populate)
    //   查 CopyContext——如果这个引用的目标恰好也在本次操作范围内被复制过, 重定向到那个副本,
    //   否则保留指向原对象, 不强行新建。这样"只复制单个对象"和"连同它 Deep 引用到的子树一起
    //   复制"用的是同一套机制, 行为随本次操作实际涉及的范围自动正确。
    //
    // 一个 Copier 实例绑定一份 CopyContext, 生命周期内只应该用来操作这一份——这也是为什么它
    // 不是一组静态函数: 复制一整棵子树时需要多次调用 copy(), 且必须共享同一份 CopyContext(子
    // 节点的 Shallow 引用要能查到父节点的副本), 绑定成实例成员之后, "共享同一份 context"是
    // 类型层面的保证, 不再是调用方需要自己记住的约定。
    class Copier
    {
    public:
        explicit Copier(CopyContext& context) noexcept : mContext(context) {}

        // 只扫描: 分配 original(以及它通过 Deep 属性能到达的一切)的空副本, 不填充任何属性。
        // 返回值已经是对应副本的身份(指针不变), 但属性要等 finalizeAll() 填充完才有效。
        // 复制多个互相有 Shallow 引用的根对象(比如子节点的 Transform::mParent 指向另一个
        // 根节点的 Transform)时, 对每个根各调一次这个方法(同一个 Copier 实例), 全部调用完
        // 之后统一调一次 finalizeAll()——这样任意两个根之间的 Shallow 引用, 不管 copy() 的
        // 调用顺序是什么, 填充阶段都能查到彼此已经分配好的副本, 不会退化成指向原对象。
        Object* copy(Object* original, Object* newOwner);

        // 填充这份 CopyContext 里所有还没填充的副本(可能来自一次或多次 copy() 调用), 再
        // 按分配顺序对每个副本调用 finalizeCopy(), 处理跨对象的一致性修正(比如 Transform
        // 根据已经落定的 mParent 反过来重建 mChildren)。整个拷贝操作只应该调一次——重复
        // 调用会让 finalizeCopy() 被同一个副本执行多次。
        void finalizeAll();

    private:
        // 扫描阶段: 只负责"发现需要新分配的对象、提前分配好空副本", 不填充任何属性。
        void scan(Object* original, Object* newOwner);
        void scanValue(Type* type, CopyPolicy policy, const void* src, Object* newOwner);

        // 填充阶段: 把 original 的属性值搬进 copy(此时 copy 已经在扫描阶段分配好)。
        void populate(Object* original, Object* copy);
        void copyProperty(Property* property, const void* srcInstance, void* dstInstance, Object* newOwner);
        void copyContainerContent(Type* type, CopyPolicy policy, const void* src, void* dst, Object* newOwner);

        // 处理容器里单个槽位(数组元素/set 元素/map 的 key 或 value/pair 的 first 或 second/
        // variant 当前激活的分支): 槽位类型本身就是 Object 引用就按策略解析后调用 setter 写入;
        // 完全不含 Object 引用就把原始值指针原样交给 setter(setter 内部走真实的 C++ 赋值语义,
        // 足够正确); 槽位类型是"结构体/容器又套了一层、且内部含 Object 引用"这种更深嵌套,
        // 构造一个同类型的临时实例递归填充, 再整体交给 setter——递归深度和槽位数量都不做限制。
        void copySlot(
            Type* slotType,
            CopyPolicy policy,
            const void* srcSlotPtr,
            Object* newOwner,
            const std::function<void(const void*)>& setter);

        // Shallow/Deep 引用的统一解析: Deep 总能在 CopyContext 里查到(扫描阶段已经保证);
        // Shallow 命中本次操作范围内的某个副本就重定向, 否则保留指向原对象。
        Object* resolveReference(Object* original, CopyPolicy policy, Object* newOwner);

        // 判断一个类型(递归地, 包括 Structure 自己的属性、Special 容器的元素/键/值/变体分支)
        // 是不是含有 Object 引用槽位——决定一个属性该走"整体值拷贝"这条捷径, 还是要下探处理。
        // 不限制一个类型里能有几个这样的槽位。跟具体某一次拷贝操作的上下文无关, 留作静态方法。
        static bool containsObjectReference(Type* type);

        // 按继承链从 clazz 一直到 Object, 收集每一层自己声明的属性(Class::getProperties()
        // 只返回这一层的, 不含基类, 跟 Serializer::saveClass 里的写法保持一致)。同样跟具体
        // 某一次拷贝操作的上下文无关, 留作静态方法。
        static std::vector<Property*> collectAllProperties(Class* clazz);

    private:
        CopyContext& mContext;
    };

    // 单个根对象的简便封装: 内部自建一份只服务这一次调用的 CopyContext, 复制完成后立即
    // finalizeAll(), 直接返回强引用——对应 createObject<T>() 之于 Object 构造的角色, 是
    // Copier 最常见用法(复制一个独立对象, 不需要跟别的 copy 调用共享身份映射)的一步到位版本。
    //
    // 如果要连同子树一起复制(多个根对象需要互相看到对方、比如子节点的 Parent 要重定向到
    // 父节点的副本), 不能用这个便捷函数——那种场景需要调用方自己持有一份 CopyContext 和
    // 一个绑定它的 Copier 实例, 对每个根对象分别调 copy(), 全部调用完之后再统一调一次
    // copier.finalizeAll()。
    template <ObjectType T>
    TRef<T> copyObject(T* original, Object* newOwner)
    {
        if (!original)
            return nullptr;

        CopyContext context;
        Copier copier(context);
        Object* copy = copier.copy(original, newOwner);
        copier.finalizeAll();
        return static_cast<T*>(copy);
    }
}
