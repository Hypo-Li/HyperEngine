#include "ImGuiClassCombo.h"
#include "ImGuiFilterableCombo.h"

#include <Core/Reflection/Class.h>

#include <algorithm>
#include <unordered_map>
#include <vector>

namespace hyp
{
    namespace
    {
        void collectClasses(Class* clazz, bool includeAbstract, std::vector<Class*>& out)
        {
            if (includeAbstract || !clazz->isAbstract())
                out.push_back(clazz);
            for (Class* derived : clazz->getDerivedClasses())
                collectClasses(derived, includeAbstract, out);
        }

        // Class 的继承树在运行期是静态的(所有 Class 都在启动时通过静态初始化一次性注册
        // 完毕, 没有运行期新增/卸载的机制)——每帧重新递归遍历一遍 baseClass 的派生树再排序
        // 纯属浪费, 每个 (baseClass, includeAbstract) 组合只在第一次用到时收集+排序一次,
        // 之后跨帧、跨调用点直接复用。includeAbstract 两种状态的候选集本来就不同(排除抽象类
        // 之后是另一份列表, 不是简单地从全量里筛), 分两张表各自缓存, 不共用。
        const std::vector<Class*>& getOrCollectClasses(Class* baseClass, bool includeAbstract)
        {
            static std::unordered_map<Class*, std::vector<Class*>> cacheIncludeAbstract;
            static std::unordered_map<Class*, std::vector<Class*>> cacheExcludeAbstract;
            auto& cache = includeAbstract ? cacheIncludeAbstract : cacheExcludeAbstract;

            auto it = cache.find(baseClass);
            if (it != cache.end())
                return it->second;

            std::vector<Class*> classes;
            collectClasses(baseClass, includeAbstract, classes);
            std::sort(classes.begin(), classes.end(), [](Class* a, Class* b)
            {
                return a->getName().toStringView() < b->getName().toStringView();
            });
            return cache.emplace(baseClass, std::move(classes)).first->second;
        }
    }

    bool ClassCombo(const char* strId, Class* baseClass, Class** value, bool includeAbstract)
    {
        const std::vector<Class*>& classes = getOrCollectClasses(baseClass, includeAbstract);

        const char* preview = *value ? (*value)->getDisplayName().toCString() : "(None)";

        bool changed = false;
        if (BeginFilterableCombo(strId, preview))
        {
            for (Class* clazz : classes)
            {
                if (FilterableComboItem(clazz->getDisplayName().toCString(), clazz == *value))
                {
                    *value = clazz;
                    changed = true;
                }
            }
            EndFilterableCombo();
        }
        return changed;
    }
}
