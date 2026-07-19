#include "SerializeContext.h"

namespace hyp
{
    SerializeContext::SerializeContext(AssetManager& assetManager, bool isLoading, Guid rootGuid) :
        mAssetManager(assetManager),
        mIsLoading(isLoading),
        mRootGuid(rootGuid)
    {
    }

    SerializeContext::~SerializeContext()
    {
    }

    int32_t SerializeContext::getOrRegisterObject(Object* object, bool& isNew)
    {
        assert(mIsLoading == false);
        if (object == nullptr)
        {
            isNew = false;
            return 0;
        }

        bool isImported = false;
        Object* root = object->getRoot();
        if (root)
        {
            isImported = root->getGuid() != mRootGuid;
        }

        if (isImported)
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
            Asset* asset = mAssetManager.getAsset(object->getGuid());
            assert(asset);
            mImportTable.push_back(ImportTableEntry{ object, object->getClass(), object->getGuid(), asset->getPath() });
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
            bool ownerIsNew = false;
            int32_t ownerId = getOrRegisterObject(object->getOwner(), ownerIsNew);
            int32_t index = -(static_cast<int32_t>(mExportTable.size()) + 1);
            mExportTable.push_back(ExportTableEntry{ object, object->getClass(), object->getGuid(), ownerId });
            return index;
        }
    }

    std::vector<AssetImportEntry> SerializeContext::getImportEntries() const
    {
        assert(mIsLoading == false);
        std::vector<AssetImportEntry> entries;
        entries.reserve(mImportTable.size());
        for (const ImportTableEntry& entry : mImportTable)
            entries.push_back(AssetImportEntry{ entry.guid, entry.clazz->getName(), entry.assetPath });
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

    void SerializeContext::setImportEntries(const std::vector<AssetImportEntry>& entries)
    {
        assert(mIsLoading == true);
        assert(mImportTable.empty());
        mImportTable.reserve(entries.size());
        for (const AssetImportEntry& entry : entries)
            mImportTable.push_back(ImportTableEntry{ nullptr, Reflection::getClass(entry.className), entry.guid, entry.assetPath});
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
                Asset* asset = mAssetManager.getAsset(entry.guid);
                assert(asset);
                entry.object = asset->getOrLoadRootObject();
                return entry.object;
            }
        }
        else
        {
            uint32_t index = static_cast<uint32_t>(-id - 1);
            auto& entry = mExportTable[index];
            if (entry.object)
            {
                isNew = false;
                return entry.object;
            }
            else
            {
                isNew = true;
                bool ownerIsNew = false;
                Object* owner = getOrCreateObject(entry.ownerId, ownerIsNew);
                entry.object = restoreObject(entry.clazz, owner, entry.guid, ObjectFlags::Load);
                return entry.object;
            }
        }
    }


}
