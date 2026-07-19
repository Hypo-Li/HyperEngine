#include "AssetManager.h"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <fstream>

namespace hyp
{
    namespace
    {
        constexpr std::string_view kEnginePrefix = "Engine/";
        constexpr std::string_view kProjectPrefix = "Project/";
        constexpr std::string_view kAssetExtension = ".asset";

        // 尝试把 path 表示成相对 root 的路径;如果 path 根本不在 root 之下,返回 false
        bool tryMakeRelative(const std::filesystem::path& path, const std::filesystem::path& root, std::filesystem::path& outRelative)
        {
            if (root.empty())
                return false;
            std::error_code ec;
            std::filesystem::path relative = std::filesystem::relative(path, root, ec);
            if (ec || relative.empty())
                return false;
            if (relative.begin() != relative.end() && *relative.begin() == "..")
                return false;
            outRelative = relative;
            return true;
        }

        std::string normalizeAssetPath(std::string&& path)
        {
            std::replace(path.begin(), path.end(), '\\', '/');
            return path;
        }
    }

    AssetManager::AssetManager()
    {
    }

    AssetManager::~AssetManager()
    {
    }

    void AssetManager::initialize(
        const std::filesystem::path& engineAssetRoot,
        const std::filesystem::path& projectAssetRoot
    )
    {
        mEngineAssetRoot = engineAssetRoot;
        mProjectAssetRoot = projectAssetRoot;

        // 第一遍:把引擎目录和项目目录下所有 .asset 文件的 header 都读出来,
        // 只创建 Asset 外壳(rootObject 为空、状态是 Unloaded)并登记 guid/path,
        // 不做任何反序列化,不触碰任何资产的 Body。
        scanAssetDirectory(mEngineAssetRoot);
        scanAssetDirectory(mProjectAssetRoot);

        // 第二遍:引用校验。必须等两个目录都扫完、所有资产的 guid 都进了 mGuidAssetMap
        // 之后才能做这一步——否则资产 A 引用了排在扫描顺序后面才会被发现的资产 B,
        // 会被误判成"引用不存在"(B 明明存在,只是这一遍还没扫到)。
        //
        // 这里只是把结果打出来做诊断,不会把"是否断裂"这个结果缓存到 Asset 上——
        // 资产在运行时随时可能被新建/删除/移动,缓存下来的结果分分钟就会和现状不一致。
        // 真要在运行时查"某个资产现在是否有断裂引用",应该现查 mGuidAssetMap,
        // 而不是读取这里算出来的一份快照。
        for (const std::unique_ptr<Asset>& asset : mAssets)
        {
            for (const AssetImportEntry& importEntry : asset->getImportEntries())
            {
                if (mGuidAssetMap.find(importEntry.guid) == mGuidAssetMap.end())
                {
                    // TODO: 接入正式的日志系统之后,把这里换成 warning 输出——现在已经有了
                    // 完整的诊断信息(importEntry.className/assetPath),可以直接报出
                    // "asset->getPath() 引用的 assetPath(className)找不到了",
                    // 交给编辑器的重定向流程去处理。
                    assert(false && "asset has an unresolved import reference");
                }
            }
        }
    }

    void AssetManager::scanAssetDirectory(const std::filesystem::path& root)
    {
        if (root.empty() || !std::filesystem::is_directory(root))
            return;

        // 目录树可能很大,权限问题、软链接死循环这些都是真实会发生的情况,
        // 用 try/catch 兜底,避免因为某一个子目录访问失败就让整个引擎初始化崩掉。
        try
        {
            for (const std::filesystem::directory_entry& entry : std::filesystem::recursive_directory_iterator(root))
            {
                if (!entry.is_regular_file() || entry.path().extension() != kAssetExtension)
                    continue;

                std::ifstream file(entry.path(), std::ios::binary);
                if (!file.is_open())
                    continue;

                // 先读固定的 12 字节前缀(magic + version + headerByteLength),
                // 和 Asset::load() 里的做法一致:校验不过直接跳过这个文件,
                // 不影响目录里其它资产的扫描。
                uint32_t headerTip[3] = { 0 };
                file.read(reinterpret_cast<char*>(headerTip), sizeof(headerTip));
                if (!file || headerTip[0] != AssetHeader::kMagic || headerTip[1] != AssetHeader::kVersion)
                    continue;

                uint32_t headerByteLength = headerTip[2];
                if (headerByteLength < sizeof(headerTip))
                    continue; // 长度字段本身都不对,文件已经损坏

                // header 需要完整读入(包括前面已经读过的 12 字节)才能交给 AssetHeader::unpack,
                // 因为池里字符串的 index 是相对整份 header 数据算的。文件游标现在正好停在第 12 字节,
                // 不需要 seek,直接把已经读到的 12 字节拷过去,再接着往后读剩下的部分。
                std::vector<uint8_t> headerData(headerByteLength);
                std::memcpy(headerData.data(), headerTip, sizeof(headerTip));
                file.read(reinterpret_cast<char*>(headerData.data() + sizeof(headerTip)), headerByteLength - sizeof(headerTip));
                if (!file)
                    continue;

                AssetHeader header = AssetHeader::unpack(headerData);

                std::string assetPath = convertPhysicalPathToAssetPath(entry.path());

                // Asset 的构造函数是 private 的,只有 AssetManager 是它的友元;
                // std::make_unique 是在标准库内部调用 new,不在 AssetManager 的作用域里,
                // 拿不到这个友元权限,所以这里要手动 new 再交给 unique_ptr 接管。
                std::unique_ptr<Asset> asset(new Asset(*this, assetPath, nullptr));
                asset->initializeFromHeader(header);

                mGuidAssetMap[asset->getRootGuid()] = asset.get();
                mPathAssetMap[asset->getPath()] = asset.get();
                mAssets.push_back(std::move(asset));
            }
        }
        catch (const std::filesystem::filesystem_error&)
        {
            // TODO: 接入正式日志系统之后,把这里换成 warning 输出
        }
    }

    Asset* AssetManager::createAsset(const std::string& path, Object* rootObject)
    {
        if (!rootObject)
            return nullptr;

        auto it = mPathAssetMap.find(path);
        if (it != mPathAssetMap.end())
            return nullptr;

        mAssets.push_back(std::unique_ptr<Asset>(new Asset(*this, path, rootObject)));
        Asset* result = mAssets.back().get();
        mPathAssetMap[path] = result;
        mGuidAssetMap[result->getRootGuid()] = result;
        return result;
    }

    Asset* AssetManager::getAsset(const std::string& path) const
    {
        auto it = mPathAssetMap.find(path);
        if (it != mPathAssetMap.end())
            return it->second;
        return nullptr;
    }

    Asset* AssetManager::getAsset(Guid guid) const
    {
        auto it = mGuidAssetMap.find(guid);
        if (it != mGuidAssetMap.end())
            return it->second;
        return nullptr;
    }

    void AssetManager::moveAsset(Asset* asset, const std::string& newPath)
    {
    }

    void AssetManager::copyAsset(Asset* asset, const std::string& newPath)
    {
    }

    std::vector<Asset*> AssetManager::findAssetsReferencing(Guid guid) const
    {
        std::vector<Asset*> result;
        for (const std::unique_ptr<Asset>& asset : mAssets)
        {
            for (const AssetImportEntry& entry : asset->getImportEntries())
            {
                if (entry.guid == guid)
                {
                    result.push_back(asset.get());
                    break;
                }
            }
        }
        return result;
    }

    bool AssetManager::tryDeleteAsset(Asset* asset)
    {
        if (!asset)
            return false;

        // 只要还有别的资产在 import 它就拒绝删除——运行时删除只应该经过编辑器,
        // 编辑器应该先调 findAssetsReferencing 把这些资产列出来,引导用户完成
        // "替换引用" 再回来删除,而不是让这里静默产生断裂引用
        if (!findAssetsReferencing(asset->getRootGuid()).empty())
            return false;

        if (!asset->tryUnload())
            return false;

        // asset 指针在 mAssets.erase() 之后就失效了,要用到的信息得先取出来
        std::filesystem::path physicalPath = asset->getPhysicalPath();
        Guid rootGuid = asset->getRootGuid();
        std::string path = asset->getPath();

        std::error_code ec;
        std::filesystem::remove(physicalPath, ec);
        if (ec)
            return false;

        mGuidAssetMap.erase(rootGuid);
        mPathAssetMap.erase(path);

        auto it = std::find_if(mAssets.begin(), mAssets.end(),
            [asset](const std::unique_ptr<Asset>& entry) { return entry.get() == asset; });
        assert(it != mAssets.end());
        mAssets.erase(it);

        return true;
    }

    std::filesystem::path AssetManager::convertAssetPathToPhysicalPath(const std::string& assetPath)
    {
        const std::filesystem::path* root = nullptr;
        std::string relative;
        if (isEngineAsset(assetPath))
        {
            root = &mEngineAssetRoot;
            relative = assetPath.substr(kEnginePrefix.size());
        }
        else if (isProjectAsset(assetPath))
        {
            root = &mProjectAssetRoot;
            relative = assetPath.substr(kProjectPrefix.size());
        }
        else
        {
            assert(false && "asset path must start with Engine/ or Project/");
            return {};
        }

        std::filesystem::path physicalPath(relative);
        physicalPath += kAssetExtension;

        return *root / physicalPath;
    }

    std::string AssetManager::convertPhysicalPathToAssetPath(const std::filesystem::path& physicalPath)
    {
        std::filesystem::path relative;
        if (tryMakeRelative(physicalPath, mEngineAssetRoot, relative))
            return normalizeAssetPath(std::string(kEnginePrefix) + relative.replace_extension().generic_string());
        if (tryMakeRelative(physicalPath, mProjectAssetRoot, relative))
            return normalizeAssetPath(std::string(kProjectPrefix) + relative.replace_extension().generic_string());

        assert(false && "physical path is not under the engine or project asset root");
        return {};
    }

    bool AssetManager::isEngineAsset(const std::string& assetPath)
    {
        return assetPath.starts_with(kEnginePrefix);
    }

    bool AssetManager::isProjectAsset(const std::string& assetPath)
    {
        return assetPath.starts_with(kProjectPrefix);
    }

}
