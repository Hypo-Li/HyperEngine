#pragma once
#include "../Object/Object.h"
#include "../Asset/AssetManager.h"
#include "../Common/BulkData.h"

namespace hyp
{
    class SerializeContext
    {
    public:
        SerializeContext(AssetManager& assetManager, bool isLoading, Guid rootGuid);

        ~SerializeContext();

        bool isLoading() const noexcept
        {
            return mIsLoading;
        }

        int32_t getOrRegisterObject(Object* object, bool& isNew);

        Object* getOrCreateObject(int32_t id, bool& isNew);

        // save 完成后,调用方(Asset::save())用这个取出本次遍历发现的 import 列表,
        // 拼进 AssetHeader、并整体覆盖 Asset 自己缓存的 import 信息
        std::vector<AssetImportEntry> getImportEntries() const;

        // save 完成后,调用方用这个取出本次遍历发现的 export 列表,拼进 AssetHeader
        std::vector<AssetExportEntry> getExportEntries() const;

        // load 开始前,调用方(Asset::load())用这两个把 header 里已经读出来的
        // import/export 信息灌进来,getOrCreateObject 才有东西可查
        void setImportEntries(const std::vector<AssetImportEntry>& entries);
        void setExportEntries(const std::vector<AssetExportEntry>& entries);

        // 整棵树(mExportTable)读完之后统一调用一次:只处理本资产自己 export 出来的对象,
        // 不处理 mImportTable——import 进来的对象属于别的资产自己的加载会话,
        // 它们的 finalizeLoad 应该由那次会话自己负责调,这里不重复调
        void finalizeLoad();

        // load 方向:本次反序列化关联的资产文件里 BulkSection 的位置,由 Asset::load() 在读完
        // header 之后设置;Serializer::serializeBulkData 用它来给每个 BulkData 绑定来源。
        void setBulkDataSource(BulkDataSource source);
        const BulkDataSource& getBulkDataSource() const;

        // save 方向:Serializer 遍历过程中每写一个 BulkData 就记一条;整棵树写完、文件真正落盘
        // (或落盘失败)之后,调用方(Asset::save())调用下面这一对方法批量落地或回滚。
        struct BulkDataSaveEntry
        {
            BulkData* bulkData;
            uint64_t offset;
            uint64_t size;
            bool wasTemporarilyLoaded;
        };
        void recordBulkDataSave(BulkData* bulkData, uint64_t offset, uint64_t size, bool wasTemporarilyLoaded);

        // 文件已确认落盘成功后调用:把这次实际写入的 (source, offset, size) 落到每个记录过的 BulkData 上
        void commitBulkDataSaves(const BulkDataSource& source);

        // 落盘失败后调用:不改动任何 BulkData 的 source/offset/size,只回滚临时 load 过的内存状态
        void abortBulkDataSaves();

        // save 方向:BulkSection 的内容本身——一段和具体编码格式(二进制/文本/……)无关的原始字节流,
        // 所以放在 SerializeContext 而不是 Archive 里管理:每种 Archive(BinarySaver、以后可能的
        // JSON Saver 等)只负责把结构化数据编码进 Body,不需要各自重复实现"怎么攒 BulkSection"。
        // 把一段原始字节追加到 BulkSection,返回追加前的偏移(相对 BulkSection 起始)。
        uint64_t appendBulkSection(const void* data, uint64_t size);

        // Asset::save() 用这个把 BulkSection 拼在 body 后面写进文件
        const std::vector<uint8_t>& getBulkSection() const noexcept
        {
            return mBulkSection;
        }

    private:
        AssetManager& mAssetManager;
        bool mIsLoading;
        Guid mRootGuid;
        struct ImportTableEntry
        {
            TRef<Object> object;
            Class* clazz;
            Guid guid;
            std::string assetPath;
        };
        std::vector<ImportTableEntry> mImportTable;
        struct ExportTableEntry
        {
            TRef<Object> object;
            Class* clazz;
            Guid guid;
            int32_t ownerId;
        };
        std::vector<ExportTableEntry> mExportTable;
        BulkDataSource mBulkDataSource;
        std::vector<BulkDataSaveEntry> mBulkDataSaveEntries;
        std::vector<uint8_t> mBulkSection;
    };
}
