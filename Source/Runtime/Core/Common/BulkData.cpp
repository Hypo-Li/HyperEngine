#include "BulkData.h"
#include <cassert>
#include <fstream>

namespace hyp
{
    BulkData::BulkData(const BulkData& other) :
        mSource(other.mSource),
        mOffset(other.mOffset),
        mSize(other.mSize),
        mState(other.mState.load(std::memory_order_acquire)),
        mData(other.mData),
        mDirty(other.mDirty)
    {
    }

    BulkData& BulkData::operator=(const BulkData& other)
    {
        if (this != &other)
        {
            mSource = other.mSource;
            mOffset = other.mOffset;
            mSize = other.mSize;
            mState.store(other.mState.load(std::memory_order_acquire), std::memory_order_release);
            mData = other.mData;
            mDirty = other.mDirty;
        }
        return *this;
    }

    BulkData::BulkData(BulkData&& other) noexcept :
        mSource(std::move(other.mSource)),
        mOffset(other.mOffset),
        mSize(other.mSize),
        mState(other.mState.load(std::memory_order_relaxed)),
        mData(std::move(other.mData)),
        mDirty(other.mDirty)
    {
        other.mOffset = 0;
        other.mSize = 0;
        other.mState.store(State::Unloaded, std::memory_order_relaxed);
        other.mDirty = false;
    }

    BulkData& BulkData::operator=(BulkData&& other) noexcept
    {
        if (this != &other)
        {
            mSource = std::move(other.mSource);
            mOffset = other.mOffset;
            mSize = other.mSize;
            mState.store(other.mState.load(std::memory_order_relaxed), std::memory_order_relaxed);
            mData = std::move(other.mData);
            mDirty = other.mDirty;

            other.mOffset = 0;
            other.mSize = 0;
            other.mState.store(State::Unloaded, std::memory_order_relaxed);
            other.mDirty = false;
        }
        return *this;
    }

    void BulkData::load()
    {
        if (mSize == 0)
        {
            State expected = State::Unloaded;
            mState.compare_exchange_strong(expected, State::Loaded);
            return;
        }

        State expected = State::Unloaded;
        if (!mState.compare_exchange_strong(expected, State::Loading))
            return;

        assert(!mSource.physicalPath.empty() && "BulkData has no backing source to load from");

        std::ifstream file(mSource.physicalPath, std::ios::binary);
        assert(file.is_open());
        file.seekg(static_cast<std::streamoff>(mSource.bulkSectionOffset + mOffset), std::ios::beg);

        std::vector<uint8_t> data(mSize);
        file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(mSize));
        assert(file);

        mData = std::move(data);
        mState.store(State::Loaded, std::memory_order_release);
    }

    void BulkData::unload()
    {
        State current = mState.load(std::memory_order_acquire);
        if (current != State::Loaded)
        {
            assert(current != State::Loading && "cannot unload a BulkData while it is loading");
            return;
        }

        // 脏数据只存在于内存里,磁盘上没有对应的副本可以 reload 回来,unload 唯一安全的做法就是
        // 静默地什么都不做——这是 unload() 平常就该有的一条分支,不是"调用方用错了"才会走到的
        // 异常情况(比如刚烘焙/上传完、这份 BulkData 从没被哪个 Asset::save() 存过盘,调用方
        // 完全不需要、也不该被要求提前判断"存过盘没有"才敢调 unload())
        if (mDirty)
            return;

        State expected = State::Loaded;
        if (!mState.compare_exchange_strong(expected, State::Unloaded))
            return;

        mData.clear();
        mData.shrink_to_fit();
    }

    std::span<const uint8_t> BulkData::getData() const noexcept
    {
        assert(getState() == State::Loaded && "BulkData must be loaded before accessing its data");
        return std::span<const uint8_t>(mData.data(), mData.size());
    }

    void BulkData::setData(std::vector<uint8_t> data) noexcept
    {
        assert(getState() != State::Loading && "cannot set data while a load is in flight");
        mSize = data.size();
        mData = std::move(data);
        mDirty = true;
        mState.store(State::Loaded, std::memory_order_release);
    }

    void BulkData::bindSource(BulkDataSource source, uint64_t offset, uint64_t size) noexcept
    {
        mSource = std::move(source);
        mOffset = offset;
        mSize = size;
        mData.clear();
        mDirty = false;
        mState.store(State::Unloaded, std::memory_order_relaxed);
    }

    std::span<const uint8_t> BulkData::acquireForSave(bool& outWasTemporarilyLoaded)
    {
        outWasTemporarilyLoaded = false;
        if (mSize == 0)
            return {};

        if (getState() != State::Loaded)
        {
            assert(!mDirty && "a dirty BulkData is always Loaded; this indicates a bug");
            load();
            outWasTemporarilyLoaded = true;
        }
        return std::span<const uint8_t>(mData.data(), mData.size());
    }

    void BulkData::commitSave(BulkDataSource source, uint64_t offset, uint64_t size, bool wasTemporarilyLoaded) noexcept
    {
        mSource = std::move(source);
        mOffset = offset;
        mSize = size;
        mDirty = false;
        if (wasTemporarilyLoaded)
        {
            mData.clear();
            mData.shrink_to_fit();
            mState.store(State::Unloaded, std::memory_order_release);
        }
    }

    void BulkData::abortSave(bool wasTemporarilyLoaded) noexcept
    {
        if (wasTemporarilyLoaded)
        {
            mData.clear();
            mData.shrink_to_fit();
            mState.store(State::Unloaded, std::memory_order_release);
        }
    }
}
