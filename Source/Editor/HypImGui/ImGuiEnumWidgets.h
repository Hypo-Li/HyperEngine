#pragma once
#include <imgui.h>

#include <Core/Reflection/Reflection.h>

#include <functional>

namespace hyp
{
    class Enumeration;

    // 单选枚举下拉框; 位掩码枚举用 EnumFlagsCombo。valuePtr 指向 enumType->getSize() 大小的存储。
    bool EnumCombo(const char* strId, Enumeration* enumType, void* valuePtr);

    // 被判 false 的取值不出现在下拉列表里, 不是画出来禁用。
    bool EnumCombo(const char* strId, Enumeration* enumType, void* valuePtr, const std::function<bool(uint64_t)>& isValueEnabled);

    // 位掩码枚举: 每个 flag 一个复选框, 预览文字逗号分隔已勾选项。
    bool EnumFlagsCombo(const char* strId, Enumeration* enumType, void* valuePtr);

    template <typename T>
    bool EnumCombo(const char* strId, T* valuePtr)
    {
        return EnumCombo(strId, Reflection::getEnumeration<T>(), valuePtr);
    }

    template <typename T>
    bool EnumCombo(const char* strId, T* valuePtr, const std::function<bool(T)>& isValueEnabled)
    {
        return EnumCombo(strId, Reflection::getEnumeration<T>(), valuePtr,
            [&isValueEnabled](uint64_t value) { return isValueEnabled(static_cast<T>(value)); });
    }

    template <typename T>
    bool EnumFlagsCombo(const char* strId, T* valuePtr)
    {
        return EnumFlagsCombo(strId, Reflection::getEnumeration<T>(), valuePtr);
    }
}
