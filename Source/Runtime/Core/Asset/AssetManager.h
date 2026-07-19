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
        template <typename LAMBDA>
        void forEachAsset(LAMBDA lambda, Class* clazz = nullptr, bool includeDerived = false)
        {
            for (auto& asset : mAssets)
            {
                if (clazz)
                {
                    Class* rootClass = asset->getRootClass();
                    if (clazz == rootClass || (includeDerived && rootClass->isDerivedFrom(clazz)))
                        lambda(asset.get());
                }
                else
                {
                    lambda(asset.get());
                }
            }
        }

        void moveAsset(Asset* asset, const std::string& newPath);

        void copyAsset(Asset* asset, const std::string& newPath);

        // 现查、不缓存——谁在 import 这个 guid 会随时间变化,缓存下来的结果分分钟过期。
        // 编辑器的"删除"UI 应该先调这个,把结果展示给用户选重定向目标;
        // tryDeleteAsset 内部也用它来兜底拒绝删除。
        std::vector<Asset*> findAssetsReferencing(Guid guid) const;

        bool tryDeleteAsset(Asset* asset);

        std::filesystem::path convertAssetPathToPhysicalPath(const std::string& assetPath);

        std::string convertPhysicalPathToAssetPath(const std::filesystem::path& physicalPath);

        static bool isEngineAsset(const std::string& assetPath);

        static bool isProjectAsset(const std::string& assetPath);

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
