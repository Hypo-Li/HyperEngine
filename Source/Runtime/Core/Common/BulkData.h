#pragma once
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace hyp
{
    // 描述一个 BulkData 在磁盘上的物理来源:哪个 .asset 文件、该文件里 BulkSection 的绝对起始偏移。
    // 由 Asset::load()/save() 各自构造一份,按值传给每个 BulkData 各自持有一份拷贝——
    // 一份 path+uint64 的拷贝开销相对 BulkData 本身管理的数据量可以忽略,不需要用 shared_ptr
    // 共享同一个实例。
    struct BulkDataSource
    {
        std::filesystem::path physicalPath;
        uint64_t bulkSectionOffset = 0;
    };

    class Serializer;

    // 用于在资产里挂一块"按需加卸载"的大块二进制数据(顶点/索引数据、贴图像素等)。
    // 反序列化时只读回 offset/size(廉价,随结构化数据一起被整体加载),真正的字节要等
    // load() 被显式调用才会从磁盘读入;不需要时可以 unload() 释放。
    class BulkData
    {
        // Serializer: 遍历过程中逐实例调用 bindSource/acquireForSave
        // SerializeContext: 整棵树写完、文件真正落盘(或落盘失败)之后批量调用 commitSave/abortSave
        friend class Serializer;
        friend class SerializeContext;
    public:
        enum class State
        {
            Unloaded,
            Loading,
            Loaded,
        };

        BulkData() = default;
        ~BulkData() = default;

        BulkData(const BulkData& other);
        BulkData& operator=(const BulkData& other);

        BulkData(BulkData&& other) noexcept;
        BulkData& operator=(BulkData&& other) noexcept;

        State getState() const noexcept
        {
            return mState.load(std::memory_order_acquire);
        }

        bool isLoaded() const noexcept
        {
            return getState() == State::Loaded;
        }

        // Unloaded 状态下也能拿到——来自反序列化时写入的 mSize,不需要先 load()
        uint64_t getSize() const noexcept
        {
            return mSize;
        }

        // 同步加载:Unloaded -> Loading -> Loaded,按 mSource/mOffset/mSize 从磁盘读入 mData。
        // size == 0 时直接变成 Loaded,不发生任何 I/O。已经在 Loading/Loaded 时是 no-op。
        // 非 const:是否在内存里是 BulkData 真实的资源状态(触发磁盘 I/O、占用/释放内存),
        // 不是无关痛痒的缓存细节,不应该伪装成 const 方法。
        void load();

        // Loaded -> Unloaded,释放 mData。带着未保存的修改(dirty)时静默地什么都不做——
        // 磁盘上没有对应副本可以 reload,这是 unload() 平常就会走到的一条分支,不是错误,
        // 调用方不需要事先判断"存过盘没有"才敢调用。Loading 中调用是调用方的 bug,会 assert。
        void unload();

        std::span<const uint8_t> getData() const noexcept;

        // 立即把 state 变成 Loaded 并标脏,下次 save 时会把这份新数据写盘
        void setData(std::vector<uint8_t> data) noexcept;

        // unload() 的带走版本:字节交给调用方而不是丢弃,所以不像 unload() 那样对 dirty 止步——
        // dirty 意味着没有磁盘副本可回退,取走之后本对象一并归空
        std::vector<uint8_t> takeData() noexcept;

    private:
        // 仅供 Serializer 在反序列化(load)时调用,把来源信息灌进来
        void bindSource(BulkDataSource source, uint64_t offset, uint64_t size) noexcept;

        // 仅供 Serializer 在序列化(save)时调用:保证数据当下可读(必要时临时同步 load 一次),
        // outWasTemporarilyLoaded 告诉调用方这次是不是临时 load 的,写盘完成后要不要还原成 Unloaded
        std::span<const uint8_t> acquireForSave(bool& outWasTemporarilyLoaded);

        // 写盘成功后调用:落地这次实际写入的 (source, offset, size);
        // 如果当时是临时 load 的,顺带释放掉 mData、还原成 Unloaded
        void commitSave(BulkDataSource source, uint64_t offset, uint64_t size, bool wasTemporarilyLoaded) noexcept;

        // 写盘失败后调用:不改动 mSource/mOffset/mSize(磁盘上原来那份内容依然有效),
        // 只在临时 load 过的情况下把内存状态还原成 Unloaded
        void abortSave(bool wasTemporarilyLoaded) noexcept;

    private:
        BulkDataSource mSource;
        uint64_t mOffset = 0;
        uint64_t mSize = 0;
        std::atomic<State> mState{ State::Unloaded };
        std::vector<uint8_t> mData;
        bool mDirty = false;
    };
}
