#include "SerializeContext.h"
#include <cstring>

namespace hyp
{
    SerializeContext::SerializeContext(bool isLoading, Guid rootGuid) :
        mIsLoading(isLoading),
        mRootGuid(rootGuid)
    {
    }

    SerializeContext::~SerializeContext()
    {
    }

    bool SerializeContext::isImported(const Object* object) const
    {
        // object 自己或者它的下级是不是以 rootGuid 为根的那棵(子)树"——不要求 rootGuid 必须是
        // 整棵所有权树的最顶端。rootGuid 就是整棵树的根时(现在 Asset 存取的唯一用法), 这个判断
        // 和原来的 getRoot()->getGuid() == mRootGuid 结果完全一样; rootGuid 是树中间某个节点
        // (比如只想单独序列化挂在 Entity 上的某个 Component)时, 它的 owner(Entity、Scene)
        // 会正确地被归到 import, 不会被误当成"这次序列化也要负责写出它的内容"。
        // 在 owner 链(含 object 自己)里找到 mRootGuid, 说明 object 是这棵树的一部分, 不是
        // import; 一路找到链顶都没找到, 才说明它跟这次序列化的树无关, 是 import。
        const Object* current = object;
        while (current)
        {
            if (current->getGuid() == mRootGuid)
                return false;
            current = current->getOwner();
        }
        return true;
    }

    int32_t SerializeContext::getOrRegisterObject(Object* object, bool& isNew)
    {
        assert(mIsLoading == false);
        if (object == nullptr)
        {
            isNew = false;
            return 0;
        }

        if (isImported(object))
        {
            for (int32_t i = 0; i < mImportTable.size(); i++)
            {
                auto& entry = mImportTable[i];
                if (entry.object == object)
                {
                    isNew = false;
                    return i + 1;
                }
            }
            isNew = true;
            int32_t index = static_cast<int32_t>(mImportTable.size()) + 1;
            mImportTable.push_back(ImportTableEntry{ object, object->getClass(), object->getGuid() });
            return index;
        }
        else
        {
            for (int32_t i = 0; i < mExportTable.size(); i++)
            {
                auto& entry = mExportTable[i];
                if (entry.object == object)
                {
                    isNew = false;
                    return -(i + 1);
                }
            }
            isNew = true;
            // owner id 不在这里递归解——这条路径不经过 saveClass, 递归解出来的对象不会有人
            // 给它写 body, 会让 mExportTable 和 BinarySaver::mObjectBuffers 的下标错位。
            // owner 先占位成 0, 等整棵树遍历完、getExportEntries() 里再统一翻译成真正的 id。
            int32_t index = -(static_cast<int32_t>(mExportTable.size()) + 1);
            mExportTable.push_back(ExportTableEntry{ object, object->getClass(), object->getGuid(), 0 });
            return index;
        }
    }

    std::vector<AssetImportEntry> SerializeContext::getImportEntries() const
    {
        assert(mIsLoading == false);
        std::vector<AssetImportEntry> entries;
        entries.reserve(mImportTable.size());
        // assetPath 这里给不了——这个 guid 是不是真的对应一个 Asset、在哪个路径, 只有握着
        // AssetManager 的调用方(Asset::save())知道, 由它自己按 guid 查出来再填。
        for (const ImportTableEntry& entry : mImportTable)
            entries.push_back(AssetImportEntry{ entry.guid, entry.clazz->getName(), std::string() });
        return entries;
    }

    std::vector<AssetExportEntry> SerializeContext::getExportEntries() const
    {
        assert(mIsLoading == false);
        std::vector<AssetExportEntry> entries;
        entries.reserve(mExportTable.size());
        for (const ExportTableEntry& entry : mExportTable)
            entries.push_back(AssetExportEntry{ entry.guid, entry.clazz->getName(), entry.ownerId });
        return entries;
    }

    void SerializeContext::resolveOwners()
    {
        assert(mIsLoading == false);
        // 整棵树这时候已经遍历完, object 本身在这棵树里的, 一定已经通过正常路径注册过了——
        // 这里查到的要么是 mExportTable 里现成的一条(直接返回下标, 不会新增), 要么是这棵
        // 树之外的对象, 会被当成 import 登记(import 不用写 body, 这时候登记是安全的)。
        // 按下标而不是引用/迭代器遍历——万一上面这条假设不成立、getOrRegisterObject 真的
        // 往 mExportTable 里新增了一条, vector 扩容不会让这个循环用悬空引用。
        for (size_t i = 0; i < mExportTable.size(); ++i)
        {
            bool isNew = false;
            int32_t ownerId = getOrRegisterObject(mExportTable[i].object->getOwner(), isNew);
            // owner 要么是 mExportTable 里已经注册过的另一条(isNew 应为 false), 要么是
            // 全新的 import(isNew 为 true 但 ownerId > 0)——owner 不可能是全新的 export,
            // 断言炸了说明上面那条"整棵树已经遍历完"的假设在哪里被破坏了。
            assert(!(isNew && ownerId < 0));
            mExportTable[i].ownerId = ownerId;
        }
    }

    void SerializeContext::setImportEntries(const std::vector<AssetImportEntry>& entries)
    {
        assert(mIsLoading == true);
        assert(mImportTable.empty());
        mImportTable.reserve(entries.size());
        for (const AssetImportEntry& entry : entries)
            mImportTable.push_back(ImportTableEntry{ nullptr, Reflection::getClass(entry.className), entry.guid });
    }

    void SerializeContext::setExportEntries(const std::vector<AssetExportEntry>& entries)
    {
        assert(mIsLoading == true);
        assert(mExportTable.empty());
        mExportTable.reserve(entries.size());
        for (const AssetExportEntry& entry : entries)
        {
            Class* clazz = Reflection::getClass(entry.className);
            assert(clazz);
            mExportTable.push_back(ExportTableEntry{ nullptr, clazz, entry.guid, entry.ownerId });
        }
    }

    void SerializeContext::constructAllObjects()
    {
        assert(mIsLoading == true);
        // 表和每条记录的 owner id 这时候已经完整、调用方也已经把 registerKnownObject()
        // 调完了——直接把所有对象构造出来(owner 保证先于自己被构造, 不存在谁先谁后的问题)。
        for (int32_t i = 0; i < static_cast<int32_t>(mExportTable.size()); ++i)
            ensureObjectConstructed(-(i + 1));
    }

    Object* SerializeContext::ensureObjectConstructed(int32_t id)
    {
        assert(mIsLoading == true);
        if (id >= 0)
        {
            // id == 0(空引用)或者 id > 0(import)——两种情况 getOrCreateObject 都不会
            // 涉及 loaded、也不会有任何读 body 的副作用, 直接复用它, 不重复一遍
            // mKnownObjects 的解析逻辑。
            bool isNew = false;
            return getOrCreateObject(id, isNew);
        }

        uint32_t index = static_cast<uint32_t>(-id - 1);
        auto& entry = mExportTable[index];
        if (!entry.object)
        {
            Object* owner = ensureObjectConstructed(entry.ownerId);
            entry.object = restoreObject(entry.clazz, owner, entry.guid, ObjectFlags::Load);
        }
        return entry.object;
    }

    void SerializeContext::registerKnownObject(Guid guid, Object* object)
    {
        assert(mIsLoading == true);
        mKnownObjects[guid] = object;
    }

    void SerializeContext::finalizeLoad()
    {
        assert(mIsLoading == true);
        for (ExportTableEntry& entry : mExportTable)
        {
            if (entry.object)
                entry.object->finalizeLoad();
        }
    }

    void SerializeContext::setBulkDataSource(BulkDataSource source)
    {
        assert(mIsLoading == true);
        mBulkDataSource = std::move(source);
    }

    const BulkDataSource& SerializeContext::getBulkDataSource() const
    {
        assert(mIsLoading == true);
        return mBulkDataSource;
    }

    void SerializeContext::recordBulkDataSave(BulkData* bulkData, uint64_t offset, uint64_t size, bool wasTemporarilyLoaded)
    {
        assert(mIsLoading == false);
        mBulkDataSaveEntries.push_back(BulkDataSaveEntry{ bulkData, offset, size, wasTemporarilyLoaded });
    }

    void SerializeContext::commitBulkDataSaves(const BulkDataSource& source)
    {
        assert(mIsLoading == false);
        for (const BulkDataSaveEntry& entry : mBulkDataSaveEntries)
            entry.bulkData->commitSave(source, entry.offset, entry.size, entry.wasTemporarilyLoaded);
    }

    void SerializeContext::abortBulkDataSaves()
    {
        assert(mIsLoading == false);
        for (const BulkDataSaveEntry& entry : mBulkDataSaveEntries)
            entry.bulkData->abortSave(entry.wasTemporarilyLoaded);
    }

    void SerializeContext::setBulkSection(std::vector<uint8_t> bulkSection)
    {
        assert(mIsLoading == true);
        mBulkSection = std::move(bulkSection);
    }

    uint64_t SerializeContext::appendBulkSection(const void* data, uint64_t size)
    {
        assert(mIsLoading == false);
        uint64_t offset = static_cast<uint64_t>(mBulkSection.size());
        size_t oldSize = mBulkSection.size();
        mBulkSection.resize(oldSize + static_cast<size_t>(size));
        std::memcpy(mBulkSection.data() + oldSize, data, static_cast<size_t>(size));
        return offset;
    }

    Object* SerializeContext::getOrCreateObject(int32_t id, bool& isNew)
    {
        assert(mIsLoading == true);
        if (id == 0)
        {
            isNew = false;
            return nullptr;
        }
        if (id > 0)
        {
            uint32_t index = static_cast<uint32_t>(id - 1);
            auto& entry = mImportTable[index];
            if (entry.object)
            {
                isNew = false;
                return entry.object;
            }
            else
            {
                isNew = true;
                // 这里不认识 AssetManager, 也不该认识——import 引用具体是谁, 必须由调用方在
                // 调 serializeObject() 之前就通过 registerKnownObject() 注册好(真正的 Asset
                // 引用, 调用方自己拿 guid 去查 AssetManager 再注册进来)。完全没注册(map 里
                // 连这个 guid 都没有)说明调用方漏处理了这条 import, 是调用方的 bug, 断言;
                // 但"注册过、对应的活对象是 nullptr"是合法结果——调用方查过 AssetManager
                // 确实找不到这个资产了(比如被删了), 显式注册 nullptr 表示"我知道这个引用,
                // 它现在解析不出东西", 跟资产引用本来就允许是空引用同一个语义, 不是 bug。
                auto knownIt = mKnownObjects.find(entry.guid);
                assert(knownIt != mKnownObjects.end());
                entry.object = knownIt->second;
                return entry.object;
            }
        }
        else
        {
            // constructAllObjects() 已经把整张表的对象都构造好了(见 ensureObjectConstructed),
            // 这里只需要判断这个对象的属性有没有被读过——跟"造没造"是两件独立的事:
            // 它完全可能是作为别的对象的 owner 提前被构造出来的, 那种情况下 loaded 依然是
            // false, 轮到它自己被正常引用到时, 这里才会第一次返回 isNew = true。
            uint32_t index = static_cast<uint32_t>(-id - 1);
            auto& entry = mExportTable[index];
            assert(entry.object);
            isNew = !entry.loaded;
            if (isNew)
                entry.loaded = true;
            return entry.object;
        }
    }


}
