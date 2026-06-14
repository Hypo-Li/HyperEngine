#pragma once
#include <unordered_map>
#include <typeindex>
#include <any>
#include <cassert>

namespace hyp
{
    class PassStorage
    {
        using PairTypeIndex = std::pair<std::type_index, std::type_index>;
    public:
        template <typename KEY, typename VALUE>
        VALUE& getOrCreate()
        {
            PairTypeIndex key = std::make_pair(std::type_index(typeid(KEY)), std::type_index(typeid(VALUE)));
            auto it = mData.find(key);
            if (it == mData.end())
            {
                it = mData.emplace(key, std::make_any<VALUE>()).first;
            }
            return std::any_cast<VALUE&>(it->second);
        }

    private:
        struct PairTypeIndexHash
        {
            std::size_t operator()(const std::pair<std::type_index, std::type_index>& p) const noexcept
            {
                std::size_t h1 = p.first.hash_code();
                std::size_t h2 = p.second.hash_code();

                return h1 ^ (h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2));
            }
        };

        std::unordered_map<PairTypeIndex, std::any, PairTypeIndexHash> mData;
    };
}
