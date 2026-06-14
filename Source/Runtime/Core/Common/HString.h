#pragma once
#include <string_view>
#include <array>
#include <vector>
#include <unordered_map>
#include <memory>
#include <iostream>
#include <shared_mutex>

#include "Types.h"

namespace hyp
{
    struct HStringId
    {
        uint16_t index = UINT16_MAX;
        uint16_t offset = UINT16_MAX;

        bool isValid() const noexcept
        {
            return index != UINT16_MAX || offset != UINT16_MAX;
        }
    };

    class HString;
    class HStringPool
    {
    public:
        HStringPool() = default;

        HStringId findStringId(std::string_view string);

        std::string_view getBaseString(HStringId id) const;

        std::string_view getFullString(HString hashString);

    private:
        HStringId addString(std::string_view string, size_t hash);

        std::string_view addFullString(HString hashString, uint64_t key);

    private:
        static constexpr size_t kBlockSize = 65536;
        struct Block
        {
            std::array<char, kBlockSize> data;
            uint16_t offset = 0;
        };

        std::unordered_multimap<size_t, HStringId> mHashIdMap;
        std::vector<std::unique_ptr<Block>> mStringBlocks;
        uint16_t mCurrentBlockIndex = 0;

        std::unordered_map<uint64_t, HStringId> mKeyFullStringIdMap;
        std::vector<std::unique_ptr<Block>> mFullStringBlocks;
        uint16_t mCurrentFullStringBlockIndex = 0;

        std::shared_mutex mPoolMutex;
        std::shared_mutex mFullStringMutex;
    };

    class HString
    {
        friend class HStringPool;
    public:
        HString() = default;

        HString(std::string_view string);

        HString(const char* string) : HString(std::string_view(string)) {}

        HString(const HString&) = default;

        bool operator==(const HString& other) const noexcept;

        bool operator!=(const HString& other) const noexcept;

        bool operator<(const HString& other) const noexcept;

        HString operator()(uint32_t number) const;

        std::string_view toStringView() const noexcept;

        const char* toCString() const noexcept;

        void setNumber(uint32_t number) noexcept
        {
            mNumber = number;
        }

        uint32_t getNumber() const noexcept
        {
            return mNumber;
        }

        size_t getTypeHash() const noexcept;

        bool empty() const noexcept
        {
            return !mId.isValid();
        }

    private:
        HStringId getId() const noexcept
        {
            return mId;
        }

        void parseString(std::string_view string);

        static HStringPool& getPool();

    private:
        HStringId mId;
        uint32_t mNumber = 0;

#ifdef _DEBUG
        static HStringPool* sPoolForNatvis;
#endif
    };

    std::ostream& operator<<(std::ostream& os, const HString& hstring);

    template <FixedString Str>
    HString operator"" _hs()
    {
        static HString hstr(Str.data);
        return hstr;
    }
}

namespace std
{
    template<>
    struct hash<hyp::HString>
    {
        size_t operator()(const hyp::HString& hs) const
        {
            return hs.getTypeHash();
        }
    };
}
