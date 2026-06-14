#pragma once
#include "Resource.h"

namespace hyp::rg
{
    class Blackboard
    {
    public:
        Blackboard() = default;

        ~Blackboard() = default;

        void put(HString name, ResourceHandle handle)
        {
            mResourceHandleMap[name] = handle;
        }

        template <typename T>
        T get(HString name) const
        {
            auto it = mResourceHandleMap.find(name);
            if (it != mResourceHandleMap.end())
            {
                return static_cast<T>(it->second);
            }
            return T();
        }

    private:
        std::unordered_map<HString, ResourceHandle> mResourceHandleMap;
    };
}
