#pragma once
#include <imgui.h>

namespace hyp
{
    class Class;

    // 从 baseClass 的继承树(含 baseClass 自己)里选一个具体 Class, 用于"选一个类型去实例化"
    // 这类场景(Inspector 的 Add Component 下拉框是典型用例)。includeAbstract=false 时抽象类
    // 不出现在候选列表里——这类场景选出来的 Class 通常马上要拿去 Class::allocate(), 抽象类
    // 选了也用不了。*value 为 nullptr 表示当前还没选任何类型, 预览文字显示 "(None)"。
    // 返回 true 表示这一帧用户选了新的 Class(*value 已经被改写)。
    bool ClassCombo(const char* strId, Class* baseClass, Class** value, bool includeAbstract = false);
}
