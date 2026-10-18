#pragma once
#include "../Object/Object.h"
#include "../Common/HString.h"
#include "../Common/Guid.h"
#include "../Common/BulkData.h"
#include <filesystem>
#include <atomic>
#include <span>

namespace hyp
{
    enum class AssetState
    {
        Unloaded,
        Loading,
        Loaded,
    };

    struct AssetImportEntry
    {
        Guid guid;
        HString className;
        std::string assetPath;
    };

    // 本资产自己 export 出来的对象:guid/className/ownerId 是 SerializeContext
    // 在 load 时重建 shell 必须要有的信息,和 import 表放在同一段 header 里,
    // 只是为了和 import 保持对称——它不参与"扫全部资产"阶段的任何逻辑。
    struct AssetExportEntry
    {
        Guid guid;
        HString className;
        int32_t ownerId;
    };

    // 资产文件头:AssetManager::initialize() 在还不 load 资产本体的情况下,
    // 只读这一段就能拿到 root 身份、root 类型、以及依赖的 import 列表。
    struct AssetHeader
    {
        static constexpr uint32_t kMagic =
            (uint32_t('H') << 0) | (uint32_t('Y') << 8) | (uint32_t('P') << 16) | (uint32_t('A') << 24);
        static constexpr uint32_t kVersion = 1;

        // 不单独存 rootGuid/rootClassName——exports[0] 恒为 root(SerializeContext
        // 的注册算法保证了这一点),单独再存一份纯粹是冗余
        std::vector<AssetImportEntry> imports;
        std::vector<AssetExportEntry> exports;

        // Body 段的字节长度。有了它才能在不解析 Body 内容的前提下算出 BulkSection 在文件里的
        // 绝对起始偏移(= headerByteLength + bodyByteLength),从而只把 Header+Body 读进内存,
        // BulkSection 留在磁盘上按需读取。
        uint64_t bodyByteLength = 0;

        std::vector<uint8_t> pack() const;

        // data 必须恰好是 peekByteLength 得到的那么多字节(即完整的 header 段)
        static AssetHeader unpack(std::span<const uint8_t> data);

        // 只读最前面的 12 字节(magic + version + headerByteLength),
        // 用于在不读整个文件的情况下,得知该再读多少字节才是完整的 header、
        // 以及 body 在文件中的起始偏移(== 返回值)。
        static uint32_t peekByteLength(std::span<const uint8_t> prefix);

    private:
        static void appendBytes(std::vector<uint8_t>& output, const void* data, size_t size);
        static void appendString(std::vector<uint8_t>& output, std::string_view str);

        static void readBytes(std::span<const uint8_t> data, size_t& cursor, void* dst, size_t size);
        static std::string readString(std::span<const uint8_t> data, size_t& cursor);
    };

    class AssetManager;
    class Asset
    {
        friend class AssetManager;
    public:
        AssetState getState() const noexcept
        {
            return mState;
        }

        const std::string& getPath() const noexcept
        {
            return mPath;
        }

        const std::filesystem::path& getPhysicalPath() const noexcept
        {
            return mPhysicalPath;
        }

        HString getName() const noexcept
        {
            return mName;
        }

        // 整个资产系统(注册表 + 每个 Asset 自己的数据)约定只在主线程访问——
        // 反序列化过程会经过 ObjectMemoryPool 构造 Object,而它本身不是线程安全的,
        // 所以这个约定不是可选项。真正可以放到后台线程的只有纯粹的磁盘 I/O
        // (读字节到内存),不涉及这里任何字段。基于这个前提,以下这些方法都不需要加锁。
        Guid getRootGuid() const noexcept
        {
            return mRootGuid;
        }

        Class* getRootClass() const noexcept
        {
            return mRootClass;
        }

        // header 里记录的、上次 save 时发现的依赖列表(诊断/引用校验用,不代表当前内存状态)。
        // 返回拷贝而不是引用——防的不是并发,是调用方把结果跨帧存起来之后,
        // mImportEntries 被后续 save() 换掉,拿在手上的引用/视图变成悬空的
        std::vector<AssetImportEntry> getImportEntries() const
        {
            return mImportEntries;
        }

        // header 里记录的、上次 save 时发现的完整 export 列表——load 时直接喂给
        // SerializeContext 用,不需要再重新解析一遍 header。同样返回拷贝,理由同上
        std::vector<AssetExportEntry> getExportEntries() const
        {
            return mExportEntries;
        }

        // 返回 TRef 而不是裸指针,理由同上:调用方可能把它存起来跨帧用,
        // 这期间 tryUnload() 可能会把 mRootObject 清掉——TRef 自己的引用计数
        // 能保证只要调用方还攥着它,对象就不会被销毁
        template <ObjectType T = Object>
        TRef<T> getRootObject() const noexcept
        {
            if (!mRootObject)
            {
                return nullptr;
            }
            assert(mRootObject->getClass()->isDerivedFrom(T::staticClass()));
            return TRef<T>(static_cast<T*>(mRootObject.get()));
        }

        template <ObjectType T = Object>
        TRef<T> getOrLoadRootObject()
        {
            if (mState.load(std::memory_order_acquire) == AssetState::Unloaded)
            {
                load();
            }
            return getRootObject<T>();
        }

        void load();

        bool tryUnload();

        bool save();

        // 只能在 Unloaded 状态下调用, 且 rootObject 的 guid/class 必须已经和这个 Asset 的一致。
        void setRootObject(Object* rootObject);

        // 无条件放弃 mRootObject 并转回 Unloaded, 不像 tryUnload() 那样检查 refcount——调用方
        // 必须保证外部持有的那份引用之后不会再触发这个 Asset 的 load()/getOrLoadRootObject(),
        // 否则会用同一个 guid 造出第二个活对象。
        void forceUnload();

        // 内存状态是否已经和磁盘不一致, 由调用方显式 setDirty(true), save() 成功时自动清掉
        bool isDirty() const noexcept
        {
            return mDirty;
        }

        void setDirty(bool dirty) noexcept
        {
            mDirty = dirty;
        }

    private:
        Asset(AssetManager& assetManager, const std::string& path, Object* rootObject);

        void initializeFromHeader(const AssetHeader& header);

        // mPath 的最后一段(去掉目录前缀)——AssetManager 每次改写 mPath(移动/重命名)之后
        // 都要重新调用这个保持同步, 不能只在构造时算一次。
        void refreshNameFromPath();

    private:
        AssetManager& mAssetManager;
        std::string mPath;
        std::filesystem::path mPhysicalPath;
        HString mName;
        Guid mRootGuid;
        Class* mRootClass;
        std::vector<AssetImportEntry> mImportEntries;
        std::vector<AssetExportEntry> mExportEntries;
        TRef<Object> mRootObject;

        std::atomic<AssetState> mState = AssetState::Unloaded;
        bool mDirty = false;
    };
}
