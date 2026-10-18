#include "ImGuiEnumWidgets.h"
#include "ImGuiFilterableCombo.h"

#include <Core/Reflection/Enumeration.h>

#include <string>

namespace hyp
{
    bool EnumCombo(const char* strId, Enumeration* enumType, void* valuePtr)
    {
        return EnumCombo(strId, enumType, valuePtr, nullptr);
    }

    bool EnumCombo(const char* strId, Enumeration* enumType, void* valuePtr, const std::function<bool(uint64_t)>& isValueEnabled)
    {
        uint64_t currentValue = enumType->getValue(valuePtr);
        size_t currentIndex = enumType->getIndexByValue(currentValue);
        const char* preview = currentIndex < enumType->getValueCount()
            ? enumType->getNameByIndex(currentIndex).toCString()
            : "(Unknown)";

        bool changed = false;
        if (BeginFilterableCombo(strId, preview))
        {
            for (size_t i = 0; i < enumType->getValueCount(); ++i)
            {
                uint64_t value = enumType->getValueByIndex(i);
                if (isValueEnabled && !isValueEnabled(value))
                    continue;

                if (FilterableComboItem(enumType->getNameByIndex(i).toCString(), i == currentIndex))
                {
                    enumType->setValue(valuePtr, value);
                    changed = true;
                }
            }
            EndFilterableCombo();
        }
        return changed;
    }

    bool EnumFlagsCombo(const char* strId, Enumeration* enumType, void* valuePtr)
    {
        std::vector<HString> flagNames = enumType->getFlagNames(valuePtr);
        std::string preview;
        if (flagNames.empty())
        {
            preview = "None";
        }
        else
        {
            for (size_t i = 0; i < flagNames.size(); ++i)
            {
                if (i > 0)
                    preview += ", ";
                preview += flagNames[i].toStringView();
            }
        }

        bool changed = false;
        if (BeginFilterableCombo(strId, preview.c_str()))
        {
            uint64_t currentValue = enumType->getValue(valuePtr);
            for (size_t i = 0; i < enumType->getValueCount(); ++i)
            {
                uint64_t flagValue = enumType->getValueByIndex(i);
                if (flagValue == 0)
                    continue; // "None"/零值项不是一个可勾选的 flag, 跳过

                const char* name = enumType->getNameByIndex(i).toCString();
                if (!PassesFilterableComboFilter(name))
                    continue;

                bool isSet = (currentValue & flagValue) != 0;
                if (ImGui::Checkbox(name, &isSet))
                {
                    currentValue = isSet ? (currentValue | flagValue) : (currentValue & ~flagValue);
                    enumType->setValue(valuePtr, currentValue);
                    changed = true;
                }
            }
            EndFilterableCombo();
        }
        return changed;
    }
}
