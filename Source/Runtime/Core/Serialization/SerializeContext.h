#pragma once
#include "../Object/Object.h"
#include "../Asset/Asset.h"
#include "../Common/BulkData.h"

#include <unordered_map>

namespace hyp
{
    // 不依赖 AssetManager——guid 怎么解析成活对象完全交给调用方决定(见 registerKnownObject()),
    // 这里只管按 isRootedAt() 的结果把引用分进 import/export 表, 记 guid, 不查、也不假设
    // 这个 guid 背后是不是一个真正的 Asset。AssetImportEntry.assetPath 这类"这个 guid 对应
    // 哪个 Asset 文件"的信息是 Asset::save()/load() 自己的事——它们已经握着 AssetManager,
    // 用 getImportEntries() 拿到这里的 guid 列表之后, 自己去查、自己填进最终写盘的表里。
    class SerializeContext
    {
    public:
        SerializeContext(bool isLoading, Guid rootGuid);

        ~SerializeContext();

        bool isLoading() const noexcept
        {
            return mIsLoading;
        }

        int32_t getOrRegisterObject(Object* object, bool& isNew);

        Object* getOrCreateObject(int32_t id, bool& isNew);

        // 只读版的 isImported 判断, 不登记进任何表——TSoftRef 存 save 时用它先确认目标是不是
        // 外部对象, 只有外部对象才能安全调用 getOrRegisterObject() 登记进导入表; 内部对象绝不能
        // 顺手调 getOrRegisterObject(), 否则会跟目标真正的 owner 字段抢 export 表的"第一次
        // 注册"名额, 导致目标内容漏写(见 getOrRegisterObject 的 isNew 语义)。
        bool isImported(const Object* object) const;

        // save 完成后,调用方(Asset::save())用这个取出本次遍历发现的 import 列表,
        // 拼进 AssetHeader、并整体覆盖 Asset 自己缓存的 import 信息。调用之前
        // Serializer::serializeObject() 已经调过 resolveOwners(), 这里单纯读表。
        std::vector<AssetImportEntry> getImportEntries() const;

        // save 完成后,调用方用这个取出本次遍历发现的 export 列表,拼进 AssetHeader
        std::vector<AssetExportEntry> getExportEntries() const;

        // load 开始前,调用方(Asset::load())用这两个把 header 里已经读出来的
        // import/export 信息灌进来,getOrCreateObject 才有东西可查
        void setImportEntries(const std::vector<AssetImportEntry>& entries);
        void setExportEntries(const std::vector<AssetExportEntry>& entries);

        // load 开始前, 调用方必须给 getImportEntries() 里出现的每一个 guid 都注册一份
        // "guid 对应哪个活对象"——这里不认识 AssetManager, 也没有别的办法解析 import 引用。
        // 真正的 Asset 引用, 调用方(Asset::load())自己拿 guid 去查 AssetManager 再注册进来;
        // rootGuid 不是整棵树的根时(比如只序列化挂在 Entity 上的某个 Component), import
        // 表里还会出现它的 owner——那不是真正的 Asset, 调用方本来就知道具体是谁(比如"挂回
        // 哪个 Entity"是调用方决定的), 同样通过这里注册进来。getOrCreateObject() 解析
        // import 引用时只查这份表, 查不到就是调用方漏注册了, 不会退回去问 AssetManager。
        void registerKnownObject(Guid guid, Object* object);

        // 调用方按不同来源(比如"这个是 owner Scene"、"这个是某个子节点的 Transform")分别
        // 现查现注册的时候, 用这个先判断"这个 guid 是不是已经注册过", 避免落到某个统一的
        // "剩下的都当资产解析"分支时把已经注册好的对象重新覆盖成 nullptr。
        bool isKnownObject(Guid guid) const noexcept
        {
            return mKnownObjects.contains(guid);
        }

        // load 方向, 跟 resolveOwners() 对称: Serializer::serializeObject() 在真正开始读
        // 属性之前调一次, 把 mExportTable 里所有对象一次性构造出来(owner 保证先于自己被
        // 构造)。不能挂在 setExportEntries() 里提前做——这时候调用方可能还没调完
        // registerKnownObject(), 根节点自己的 owner 如果是 import(比如只反序列化单个
        // Entity, owner 是外部现查现给的 Scene), 会解析成空。
        void constructAllObjects();

        // 整棵树(mExportTable)读完之后统一调用一次:只处理本资产自己 export 出来的对象,
        // 不处理 mImportTable——import 进来的对象属于别的资产自己的加载会话,
        // 它们的 finalizeLoad 应该由那次会话自己负责调,这里不重复调
        void finalizeLoad();

        // save 方向, 跟 finalizeLoad() 对称: Serializer::serializeObject() 在整棵树遍历完
        // 之后调一次, 把每条 export 记录的 owner 补上——owner 这时候要么已经是 mExportTable
        // 里的另一条记录(直接查到, 不会新增), 要么是这棵树之外的对象, 会被当作一条新的
        // import 登记进 mImportTable(import 不需要写 body, 这时候登记是安全的, 不会有
        // 下标错位的问题)。调用方保证只调一次, 这里不做重入保护。
        void resolveOwners();

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

        // load 方向:BulkSection 已经在内存里时(如 Play 场景快照)由调用方提供, 非空则 Serializer 直接把数据拷进 BulkData
        void setBulkSection(std::vector<uint8_t> bulkSection);

    private:
        bool mIsLoading;
        Guid mRootGuid;
        struct ImportTableEntry
        {
            TRef<Object> object;
            Class* clazz;
            Guid guid;
        };
        std::vector<ImportTableEntry> mImportTable;
        std::unordered_map<Guid, Object*> mKnownObjects;
        struct ExportTableEntry
        {
            TRef<Object> object;
            Class* clazz;
            Guid guid;
            int32_t ownerId;
            // load 方向: 属性有没有从存档里读进来、应用到 object 身上——跟 object 是否已经
            // 构造完全是两回事: 它完全可能是作为别的对象的 owner、通过 ensureObjectConstructed()
            // 提前构造出来的, 那种情况下这里仍然是 false, 不能用 object 是否为空代替这个判断。
            bool loaded = false;
        };
        std::vector<ExportTableEntry> mExportTable;

        // load 方向: 只负责把这个 id 对应的对象(及其 owner 链)构造出来, 不读取任何属性、
        // 不碰 ExportTableEntry::loaded——constructAllObjects() 用它把整张表一次性构造完,
        // owner 递归调的也是它自己, 不会牵扯到"这个对象的 body 读没读过"这件事。
        Object* ensureObjectConstructed(int32_t id);

        BulkDataSource mBulkDataSource;
        std::vector<BulkDataSaveEntry> mBulkDataSaveEntries;
        std::vector<uint8_t> mBulkSection;
    };
}
