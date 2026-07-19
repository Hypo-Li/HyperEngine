#include "Asset.h"
#include "AssetManager.h"
#include "../Serialization/Serializer.h"
#include "../Serialization/BinarySaver.h"
#include "../Serialization/BinaryLoader.h"
#include <cassert>
#include <cstring>
#include <unordered_map>
#include <fstream>

namespace hyp
{
    void AssetHeader::appendBytes(std::vector<uint8_t>& output, const void* data, size_t size)
    {
        const uint8_t* bytes = static_cast<const uint8_t*>(data);
        output.insert(output.end(), bytes, bytes + size);
    }

    void AssetHeader::appendString(std::vector<uint8_t>& output, std::string_view str)
    {
        uint32_t length = static_cast<uint32_t>(str.size());
        appendBytes(output, &length, sizeof(length));
        appendBytes(output, str.data(), length);
    }

    void AssetHeader::readBytes(std::span<const uint8_t> data, size_t& cursor, void* dst, size_t size)
    {
        assert(cursor + size <= data.size());
        std::memcpy(dst, data.data() + cursor, size);
        cursor += size;
    }

    std::string AssetHeader::readString(std::span<const uint8_t> data, size_t& cursor)
    {
        uint32_t length = 0;
        readBytes(data, cursor, &length, sizeof(length));
        std::string str(length, '\0');
        if (length > 0)
            readBytes(data, cursor, str.data(), length);
        return str;
    }

    uint32_t AssetHeader::peekByteLength(std::span<const uint8_t> prefix)
    {
        size_t cursor = 0;
        uint32_t magic = 0;
        readBytes(prefix, cursor, &magic, sizeof(magic));
        assert(magic == kMagic);

        uint32_t version = 0;
        readBytes(prefix, cursor, &version, sizeof(version));
        assert(version == kVersion);

        uint32_t headerByteLength = 0;
        readBytes(prefix, cursor, &headerByteLength, sizeof(headerByteLength));
        return headerByteLength;
    }

    std::vector<uint8_t> AssetHeader::pack() const
    {
        // 去重建池:class name 走 hstring 池,path 走 string 池
        std::vector<HString> hstringPool;
        std::unordered_map<HString, uint32_t> hstringIndexMap;
        auto internHString = [&](HString value) -> uint32_t
        {
            auto it = hstringIndexMap.find(value);
            if (it != hstringIndexMap.end())
                return it->second;
            uint32_t index = static_cast<uint32_t>(hstringPool.size());
            hstringIndexMap.emplace(value, index);
            hstringPool.push_back(value);
            return index;
        };

        std::vector<std::string> stringPool;
        std::unordered_map<std::string, uint32_t> stringIndexMap;
        auto internString = [&](const std::string& value) -> uint32_t
        {
            auto it = stringIndexMap.find(value);
            if (it != stringIndexMap.end())
                return it->second;
            uint32_t index = static_cast<uint32_t>(stringPool.size());
            stringIndexMap.emplace(value, index);
            stringPool.push_back(value);
            return index;
        };

        assert(!exports.empty() && "exports[0] must be the root object");

        std::vector<uint32_t> importClassNameIndices(imports.size());
        std::vector<uint32_t> importPathIndices(imports.size());
        for (size_t i = 0; i < imports.size(); ++i)
        {
            importClassNameIndices[i] = internHString(imports[i].className);
            importPathIndices[i] = internString(imports[i].assetPath);
        }

        // export 的 className 和 import/root 共用同一个 hstring 池——
        // 同一个 Class 在一棵树里出现很多次是常态(比如一堆 Transform),
        // 池天然去重,不会因为 export 数量多就重复存很多份同样的字符串
        std::vector<uint32_t> exportClassNameIndices(exports.size());
        for (size_t i = 0; i < exports.size(); ++i)
            exportClassNameIndices[i] = internHString(exports[i].className);

        // 先把 magic/version/headerByteLength 之后的内容拼好,
        // 这样才能知道 headerByteLength 该填多少
        std::vector<uint8_t> body;

        uint32_t hstringPoolCount = static_cast<uint32_t>(hstringPool.size());
        appendBytes(body, &hstringPoolCount, sizeof(hstringPoolCount));
        for (const HString& str : hstringPool)
            appendString(body, str.toStringView());

        uint32_t stringPoolCount = static_cast<uint32_t>(stringPool.size());
        appendBytes(body, &stringPoolCount, sizeof(stringPoolCount));
        for (const std::string& str : stringPool)
            appendString(body, str);

        uint32_t importCount = static_cast<uint32_t>(imports.size());
        appendBytes(body, &importCount, sizeof(importCount));
        for (size_t i = 0; i < imports.size(); ++i)
        {
            appendBytes(body, &imports[i].guid, sizeof(Guid));
            appendBytes(body, &importClassNameIndices[i], sizeof(uint32_t));
            appendBytes(body, &importPathIndices[i], sizeof(uint32_t));
        }

        uint32_t exportCount = static_cast<uint32_t>(exports.size());
        appendBytes(body, &exportCount, sizeof(exportCount));
        for (size_t i = 0; i < exports.size(); ++i)
        {
            appendBytes(body, &exports[i].guid, sizeof(Guid));
            appendBytes(body, &exportClassNameIndices[i], sizeof(uint32_t));
            appendBytes(body, &exports[i].ownerId, sizeof(int32_t));
        }

        std::vector<uint8_t> output;
        uint32_t magic = kMagic;
        uint32_t version = kVersion;
        uint32_t headerByteLength = static_cast<uint32_t>(
            sizeof(magic) + sizeof(version) + sizeof(headerByteLength) + sizeof(bodyByteLength) + body.size());
        appendBytes(output, &magic, sizeof(magic));
        appendBytes(output, &version, sizeof(version));
        appendBytes(output, &headerByteLength, sizeof(headerByteLength));
        appendBytes(output, &bodyByteLength, sizeof(bodyByteLength));
        output.insert(output.end(), body.begin(), body.end());

        return output;
    }

    AssetHeader AssetHeader::unpack(std::span<const uint8_t> data)
    {
        size_t cursor = 0;

        uint32_t magic = 0;
        readBytes(data, cursor, &magic, sizeof(magic));
        assert(magic == kMagic);

        uint32_t version = 0;
        readBytes(data, cursor, &version, sizeof(version));
        assert(version == kVersion);

        uint32_t headerByteLength = 0;
        readBytes(data, cursor, &headerByteLength, sizeof(headerByteLength));
        assert(headerByteLength == data.size());

        uint64_t bodyByteLength = 0;
        readBytes(data, cursor, &bodyByteLength, sizeof(bodyByteLength));

        uint32_t hstringPoolCount = 0;
        readBytes(data, cursor, &hstringPoolCount, sizeof(hstringPoolCount));
        std::vector<HString> hstringPool(hstringPoolCount);
        for (uint32_t i = 0; i < hstringPoolCount; ++i)
            hstringPool[i] = HString(readString(data, cursor));

        uint32_t stringPoolCount = 0;
        readBytes(data, cursor, &stringPoolCount, sizeof(stringPoolCount));
        std::vector<std::string> stringPool(stringPoolCount);
        for (uint32_t i = 0; i < stringPoolCount; ++i)
            stringPool[i] = readString(data, cursor);

        AssetHeader header;
        header.bodyByteLength = bodyByteLength;

        uint32_t importCount = 0;
        readBytes(data, cursor, &importCount, sizeof(importCount));
        header.imports.resize(importCount);
        for (uint32_t i = 0; i < importCount; ++i)
        {
            AssetImportEntry& entry = header.imports[i];
            readBytes(data, cursor, &entry.guid, sizeof(entry.guid));

            uint32_t classNameIndex = 0;
            readBytes(data, cursor, &classNameIndex, sizeof(classNameIndex));
            assert(classNameIndex < hstringPool.size());
            entry.className = hstringPool[classNameIndex];

            uint32_t pathIndex = 0;
            readBytes(data, cursor, &pathIndex, sizeof(pathIndex));
            assert(pathIndex < stringPool.size());
            entry.assetPath = stringPool[pathIndex];
        }

        uint32_t exportCount = 0;
        readBytes(data, cursor, &exportCount, sizeof(exportCount));
        header.exports.resize(exportCount);
        for (uint32_t i = 0; i < exportCount; ++i)
        {
            AssetExportEntry& entry = header.exports[i];
            readBytes(data, cursor, &entry.guid, sizeof(entry.guid));

            uint32_t classNameIndex = 0;
            readBytes(data, cursor, &classNameIndex, sizeof(classNameIndex));
            assert(classNameIndex < hstringPool.size());
            entry.className = hstringPool[classNameIndex];

            readBytes(data, cursor, &entry.ownerId, sizeof(entry.ownerId));
        }

        assert(!header.exports.empty() && "exports[0] must be the root object");

        return header;
    }

    Asset::Asset(AssetManager& assetManager, const std::string& path, Object* rootObject) :
        mAssetManager(assetManager),
        mPath(path),
        mRootObject(rootObject)
    {
        if (rootObject)
        {
            mRootGuid = rootObject->getGuid();
            mRootClass = rootObject->getClass();
            mState.store(AssetState::Loaded, std::memory_order_release);
        }
        mPhysicalPath = assetManager.convertAssetPathToPhysicalPath(mPath);
    }

    void Asset::initializeFromHeader(const AssetHeader& header)
    {
        // exports[0] 恒为 root(SerializeContext 的注册算法保证的),
        // 不需要 header 里单独存一份 rootGuid/rootClassName
        assert(!header.exports.empty());
        mRootGuid = header.exports[0].guid;
        mRootClass = Reflection::getClass(header.exports[0].className);

        mImportEntries = header.imports;
        mExportEntries = header.exports;
    }

    void Asset::load()
    {
        AssetState expected = AssetState::Unloaded;
        if (!mState.compare_exchange_strong(expected, AssetState::Loading))
            return; // already loading in other thread, so we just return

        std::ifstream file(mPhysicalPath, std::ios::binary);
        uint32_t magic = 0;
        uint32_t version = 0;
        uint32_t headerByteLength = 0;
        uint64_t bodyByteLength = 0;
        if (file.is_open())
        {
            file.read((char*)&magic, sizeof(magic));
            file.read((char*)&version, sizeof(version));
            file.read((char*)&headerByteLength, sizeof(headerByteLength));
            file.read((char*)&bodyByteLength, sizeof(bodyByteLength));
        }
        if (!file.is_open() || magic != AssetHeader::kMagic || version != AssetHeader::kVersion)
        {
            mState.store(AssetState::Unloaded, std::memory_order_release);
            return;
        }

        // header 已经在 AssetManager::initialize() 时被完整解析并缓存到
        // mImportEntries/mExportEntries 上了,这里不需要再读一遍、也不需要再 unpack 一遍,
        // 直接跳到 body 起始位置即可。BulkSection 紧跟在 body 之后,留在磁盘上不读——
        // 只有某个 BulkData 被显式 load() 时才会单独去读那一小段。
        std::vector<uint8_t> bodyData(bodyByteLength);
        file.seekg(headerByteLength, std::ios::beg);
        file.read((char*)bodyData.data(), bodyByteLength);

        BulkDataSource bulkDataSource;
        bulkDataSource.physicalPath = mPhysicalPath;
        bulkDataSource.bulkSectionOffset = static_cast<uint64_t>(headerByteLength) + bodyByteLength;

        SerializeContext context(mAssetManager, true, mRootGuid);
        context.setImportEntries(mImportEntries);
        context.setExportEntries(mExportEntries);
        context.setBulkDataSource(std::move(bulkDataSource));
        BinaryLoader loader;
        loader.unpack(bodyData);
        Serializer serializer(loader, context);
        Object* rootObject = nullptr;
        serializer.serializeObject(rootObject);
        mRootObject = rootObject;
        mState.store(AssetState::Loaded, std::memory_order_release);
    }

    bool Asset::tryUnload()
    {
        return false;
    }

    bool Asset::save()
    {
        if (mState.load(std::memory_order_acquire) != AssetState::Loaded)
            return false;

        // rootGuid/rootClass 是这个资产的固定身份,save 不应该改写它们,
        // 这里只做一致性检查——断言失败说明别的地方出了 bug
        assert(mRootObject->getGuid() == mRootGuid);
        assert(mRootObject->getClass() == mRootClass);

        SerializeContext context(mAssetManager, false, mRootGuid);
        BinarySaver saver;
        Serializer serializer(saver, context);
        Object* rootObject = mRootObject;
        serializer.serializeObject(rootObject);

        std::vector<uint8_t> bodyData = saver.pack();
        const std::vector<uint8_t>& bulkSection = context.getBulkSection();
        std::vector<AssetImportEntry> importEntries = context.getImportEntries();
        std::vector<AssetExportEntry> exportEntries = context.getExportEntries();

        AssetHeader header;
        header.imports = importEntries;
        header.exports = exportEntries;
        header.bodyByteLength = static_cast<uint64_t>(bodyData.size());
        std::vector<uint8_t> headerData = header.pack();

        // 先写临时文件,成功后再原子 rename 过去,避免中途失败写坏原文件。
        // 文件布局是 [Header][Body][BulkSection]。
        std::filesystem::path tempPath = mPhysicalPath;
        tempPath += ".tmp";
        {
            std::ofstream file(tempPath, std::ios::binary);
            if (!file.is_open())
            {
                context.abortBulkDataSaves();
                return false;
            }
            file.write(reinterpret_cast<const char*>(headerData.data()), headerData.size());
            file.write(reinterpret_cast<const char*>(bodyData.data()), bodyData.size());
            file.write(reinterpret_cast<const char*>(bulkSection.data()), bulkSection.size());
            if (!file)
            {
                context.abortBulkDataSaves();
                return false;
            }
        }

        std::error_code ec;
        std::filesystem::rename(tempPath, mPhysicalPath, ec);
        if (ec)
        {
            context.abortBulkDataSaves();
            return false;
        }

        // 只有确认文件已经真正落盘之后,才把这次实际写入的 (source, offset, size) 落到
        // 每个 BulkData 上——避免 save 失败时,BulkData 记着一个和磁盘上旧文件对不上的偏移。
        BulkDataSource bulkDataSource;
        bulkDataSource.physicalPath = mPhysicalPath;
        bulkDataSource.bulkSectionOffset = headerData.size() + bodyData.size();
        context.commitBulkDataSaves(bulkDataSource);

        // importEntries/exportEntries 都是派生数据,这次遍历发现的结果就是最新的事实,
        // 整体覆盖而不是合并
        mImportEntries = std::move(importEntries);
        mExportEntries = std::move(exportEntries);

        return true;
    }
}
