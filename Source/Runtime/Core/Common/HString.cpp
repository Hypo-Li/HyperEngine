#include <cassert>
#include <charconv>

#include "HString.h"
#include "Hash.h"

namespace hyp
{
#ifdef _DEBUG
    HStringPool* HString::sPoolForNatvis;
#endif

    HStringId HStringPool::findStringId(std::string_view string)
    {
        size_t hash = std::hash<std::string_view>{}(string);

        // Fast Path
        {
            std::shared_lock<std::shared_mutex> readLock(mPoolMutex);
            auto range = mHashIdMap.equal_range(hash);
            for (auto it = range.first; it != range.second; ++it)
            {
                HStringId id = it->second;
                if (getBaseString(id) == string)
                    return id;
            }
        }

        // Slow Path
        std::unique_lock<std::shared_mutex> writeLock(mPoolMutex);

        // 双重检查锁定
        auto range = mHashIdMap.equal_range(hash);
        for (auto it = range.first; it != range.second; ++it)
        {
            HStringId id = it->second;
            if (getBaseString(id) == string)
                return id;
        }

        // 确定真的没有，安全地执行写入
        return addString(string, hash);
    }

    std::string_view HStringPool::getBaseString(HStringId id) const
    {
        const char* stringData = &mStringBlocks[id.index]->data[id.offset];
        uint16_t length = *reinterpret_cast<const uint16_t*>(stringData);
        return std::string_view(stringData + 2, length);
    }

    std::string_view HStringPool::getFullString(HString hashString)
    {
        HStringId hashStringId = hashString.getId();
        uint64_t key = (static_cast<uint64_t>(hashStringId.index) << 48) |
                       (static_cast<uint64_t>(hashStringId.offset) << 32) |
                       hashString.getNumber();

        // Fast Path
        {
            std::shared_lock<std::shared_mutex> readLock(mFullStringMutex);
            auto it = mKeyFullStringIdMap.find(key);
            if (it != mKeyFullStringIdMap.end())
            {
                char* stringData = &mFullStringBlocks[it->second.index]->data[it->second.offset];
                uint16_t length = *reinterpret_cast<uint16_t*>(stringData);
                return std::string_view(stringData + 2, length);
            }
        }

        // Slow Path
        std::unique_lock<std::shared_mutex> writeLock(mFullStringMutex);

        // 双重检查锁定
        auto it = mKeyFullStringIdMap.find(key);
        if (it != mKeyFullStringIdMap.end())
        {
            char* stringData = &mFullStringBlocks[it->second.index]->data[it->second.offset];
            uint16_t length = *reinterpret_cast<uint16_t*>(stringData);
            return std::string_view(stringData + 2, length);
        }

        return addFullString(hashString, key);
    }

    HStringId HStringPool::addString(std::string_view string, size_t hash)
    {
        size_t stringLength = string.length();
        // 总长度 = 长度字段(2) + 字符串内容(len) + 结束符(1)
        size_t totalLength = 2 + stringLength + 1;
        // 向上对齐到2字节
        totalLength = (totalLength + 1) & ~1;
        // 确保一个块至少存一个字符串
        assert(totalLength <= kBlockSize);

        if (mStringBlocks.empty() || mStringBlocks[mCurrentBlockIndex]->offset + totalLength > kBlockSize)
        {
            mStringBlocks.emplace_back(std::make_unique<Block>());
            mCurrentBlockIndex = static_cast<uint16_t>(mStringBlocks.size() - 1);
        }

        auto& block = mStringBlocks[mCurrentBlockIndex];
        uint16_t offset = block->offset;
        char* dest = &block->data[offset];

        *reinterpret_cast<uint16_t*>(dest) = static_cast<uint16_t>(stringLength);
        memcpy(dest + 2, string.data(), stringLength);
        dest[totalLength - 1] = '\0';

        block->offset += static_cast<uint16_t>(totalLength);

        HStringId id{ mCurrentBlockIndex, offset };
        mHashIdMap.emplace(hash, id);

        return id;
    }

    std::string_view HStringPool::addFullString(HString hashString, uint64_t key)
    {
        uint32_t number = hashString.getNumber();
        assert(number != 0);

        std::string_view base = getBaseString(hashString.getId());

        // 预计算总长度：Base长度 + 下划线(1) + 数字位数(最多10位) + \0(1)
        size_t requiredSize = base.size() + 12;

        char stackBuf[256];
        char* heapBuf = nullptr;
        char* targetBuf = stackBuf;

        if (requiredSize > sizeof(stackBuf))
        {
            heapBuf = new char[requiredSize];
            targetBuf = heapBuf;
        }

        memcpy(targetBuf, base.data(), base.size());
        targetBuf[base.size()] = '_';
        auto [p, ec] = std::to_chars(targetBuf + base.size() + 1, targetBuf + requiredSize, number - 1);
        std::string_view fullStringView(targetBuf, p - targetBuf);

        size_t stringLength = fullStringView.size();
        size_t totalLength = 2 + stringLength + 1;
        totalLength = (totalLength + 1) & ~1;
        assert(totalLength <= kBlockSize);

        if (mFullStringBlocks.empty() || mFullStringBlocks[mCurrentFullStringBlockIndex]->offset + totalLength > kBlockSize)
        {
            mFullStringBlocks.emplace_back(std::make_unique<Block>());
            mCurrentFullStringBlockIndex = static_cast<uint16_t>(mFullStringBlocks.size() - 1);
        }

        auto& block = mFullStringBlocks[mCurrentFullStringBlockIndex];
        uint16_t offset = block->offset;
        char* dest = &block->data[offset];

        *reinterpret_cast<uint16_t*>(dest) = static_cast<uint16_t>(stringLength);
        memcpy(dest + 2, fullStringView.data(), stringLength);
        dest[totalLength - 1] = '\0';

        if (heapBuf)
        {
            delete[] heapBuf;
        }

        block->offset += static_cast<uint16_t>(totalLength);

        HStringId fullStringId{ mCurrentFullStringBlockIndex, offset };

        HStringId hashStringId = hashString.getId();
        mKeyFullStringIdMap.emplace(key, fullStringId);

        return std::string_view(dest + 2, stringLength);
    }

    HString::HString(std::string_view string)
    {
        if (string.empty())
            return;
        parseString(string);
    }

    bool HString::operator==(const HString& other) const noexcept
    {
        return mId.index == other.mId.index &&
            mId.offset == other.mId.offset &&
            mNumber == other.mNumber;
    }

    bool HString::operator!=(const HString& other) const noexcept
    {
        return !(*this == other);
    }

    bool HString::operator<(const HString& other) const noexcept
    {
        if (mId.index != other.mId.index)
            return mId.index < other.mId.index;

        if (mId.offset != other.mId.offset)
            return mId.offset < other.mId.offset;

        return mNumber < other.mNumber;
    }

    HString HString::operator()(uint32_t number) const
    {
        HString result(*this);
        result.mNumber = number;
        return result;
    }

    std::string_view HString::toStringView() const noexcept
    {
        if (!mId.isValid())
            return {};
        if (mNumber == 0)
            return getPool().getBaseString(mId);
        return getPool().getFullString(*this);
    }

    const char* HString::toCString() const noexcept
    {
        // Pool 能够保证字符串以0结尾
        return toStringView().data();
    }

    size_t HString::getTypeHash() const noexcept
    {
        uint32_t idCombined = (static_cast<uint32_t>(mId.index) << 16) | mId.offset;
        size_t seed = 0;
        Hash::combine(seed, idCombined);
        Hash::combine(seed, mNumber);
        return seed;
    }

    void HString::parseString(std::string_view string)
    {
        size_t lastUnderscore = string.find_last_of('_');

        // 尝试解析后缀数字
        if (lastUnderscore != std::string_view::npos &&
            lastUnderscore > 0 &&
            lastUnderscore < string.size() - 1)
        {
            std::string_view suffix = string.substr(lastUnderscore + 1);
            uint32_t val = 0;
            auto [ptr, ec] = std::from_chars(suffix.data(), suffix.data() + suffix.size(), val);

            if (ec == std::errc())
            {
                // 成功解析数字：BaseName 是下划线前的部分
                mId = getPool().findStringId(string.substr(0, lastUnderscore));
                mNumber = val + 1; // 约定：_0 存为 1，无后缀存为 0
                return;
            }
        }

        // 无后缀或解析失败：全名存入池
        mId = getPool().findStringId(string);
        mNumber = 0;
    }

    HStringPool& HString::getPool()
    {
        static HStringPool pool;
#ifdef _DEBUG
        sPoolForNatvis = &pool;
#endif
        return pool;
    }

    std::ostream& operator<<(std::ostream& os, const HString& hstring)
    {
        return os << hstring.toStringView();
    }

}
