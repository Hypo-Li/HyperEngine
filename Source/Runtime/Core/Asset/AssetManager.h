#pragma once
#include "Asset.h"

namespace hyp
{
    class AssetManager
    {
    public:
        AssetManager();

        ~AssetManager();

        void initialize(
            const std::filesystem::path& engineAssetRoot,
            const std::filesystem::path& projectAssetRoot
        );

        Asset* createAsset(const std::string& path, Object* rootObject);

        Asset* getAsset(const std::string& path) const;

        Asset* getAsset(Guid guid) const;

        // 资产系统(这个类 + Asset 自己的数据)约定只在主线程访问,不需要加锁——
        // 见 Asset.h 里对应的说明
        // 按 mPathAssetMap(std::map<std::string, Asset*>, 天然按路径字符串排序)迭代,
        // 不是 mAssets——调用方(比如 AssetCombo)拿到的顺序本来就是按路径排好的, 不需要
        // 自己再收集一份 vector 去 sort 一遍。
        template <typename LAMBDA>
        void forEachAsset(LAMBDA lambda, Class* clazz = nullptr, bool includeDerived = false) const
        {
            for (auto& [path, asset] : mPathAssetMap)
            {
                if (clazz)
                {
                    Class* rootClass = asset->getRootClass();
                    if (clazz == rootClass || (includeDerived && rootClass->isDerivedFrom(clazz)))
                        lambda(asset);
                }
                else
                {
                    lambda(asset);
                }
            }
        }

        // 把 asset 重命名/移动到 newPath(目前只用于同目录下改名, 但实现上没有这个限制)。
        // newPath 已经被占用、或者物理重命名失败(比如文件被占用)都会拒绝、不改动任何状态,
        // 返回 false。别的资产 import 表里记的 assetPath 只是诊断用的"上次已知路径", 不需要
        // 在这里连带更新——它们下次被 save() 时会按当前实际路径整体重新生成。
        bool moveAsset(Asset* asset, const std::string& newPath);

        // 把 oldDirPath 整个目录(可能嵌套任意层子目录/资产)重命名/移动到 newDirPath。
        // 物理上只需要重命名目录本身(操作系统会把内容原封不动带过去), 内存里则要把
        // mPathAssetMap 中所有以 oldDirPath 为前缀的路径统一替换成 newDirPath 前缀。
        // newDirPath 已存在、或者物理重命名失败都会拒绝、不改动任何状态, 返回 false。
        bool renameDirectory(const std::string& oldDirPath, const std::string& newDirPath);

        void copyAsset(Asset* asset, const std::string& newPath);

        // 现查、不缓存——谁在 import 这个 guid 会随时间变化,缓存下来的结果分分钟过期。
        // 编辑器的"删除"UI 应该先调这个,把结果展示给用户选重定向目标;
        // tryDeleteAsset 内部也用它来兜底拒绝删除。
        std::vector<Asset*> findAssetsReferencing(Guid guid) const;

        bool tryDeleteAsset(Asset* asset);

        std::filesystem::path convertAssetPathToPhysicalPath(const std::string& assetPath);

        // 跟 convertAssetPathToPhysicalPath 是同一套 Engine/Project 根路径转换逻辑, 区别是
        // 这个转的是目录("Engine"/"Project" 根本身、或者它们下面的子目录路径), 不拼 .asset
        // 扩展名。AssetBrowser 浏览目录树/新建文件夹/重命名目录都要用到, 统一放在这里,
        // 不要在编辑器那边再重复一份。
        std::filesystem::path convertAssetDirectoryToPhysicalPath(const std::string& virtualDirPath) const;

        std::string convertPhysicalPathToAssetPath(const std::filesystem::path& physicalPath);

        static bool isEngineAsset(const std::string& assetPath);

        static bool isProjectAsset(const std::string& assetPath);

        const std::filesystem::path& getEngineAssetRoot() const noexcept
        {
            return mEngineAssetRoot;
        }

        const std::filesystem::path& getProjectAssetRoot() const noexcept
        {
            return mProjectAssetRoot;
        }

    private:
        // 扫描 root 目录下所有 .asset 文件,读各自的 header,构造 Asset 并登记进 mPathAssetMap/mGuidAssetMap。
        // 只做单个目录,initialize() 里对引擎目录和项目目录各调一次。
        void scanAssetDirectory(const std::filesystem::path& root);

    private:
        std::filesystem::path mEngineAssetRoot;
        std::filesystem::path mProjectAssetRoot;
        std::map<std::string, Asset*> mPathAssetMap;
        std::unordered_map<Guid, Asset*> mGuidAssetMap;
        std::vector<std::unique_ptr<Asset>> mAssets;
    };
}
