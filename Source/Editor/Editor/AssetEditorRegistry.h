#pragma once
#include "AssetEditorTab.h"
#include "AssetEditorTabHost.h"

#include <Core/Asset/Asset.h>

#include <functional>
#include <memory>
#include <unordered_map>

namespace hyp
{
    class Class;
}

namespace hyp::ed
{
    class ThumbnailProvider;
    class AssetBrowser;

    // Class(资产的 rootClass) -> "打开/聚焦对应编辑器标签页"的注册表, 跟 ComponentPanelRegistry
    // 是同一个思路——AssetBrowser 双击资产时只管调 tryOpenEditor(), 不需要认识任何具体的
    // 标签页/编辑器窗口类型; 以后新增资产类型的编辑器只需要在各自的 .cpp 里加一个
    // AssetEditorRegistrar<AssetT, TabT> 静态实例自注册, 不用回来改 AssetBrowser。
    //
    // 按 Guid 去重这一步统一在 tryOpenEditor() 里做——不管 TabT 具体是哪个子类, 这个判断
    // 都一样, 不需要每个 Registrar 各自重复。
    class AssetEditorRegistry
    {
    public:
        using Factory = std::function<std::unique_ptr<AssetEditorTab>(Asset&, ThumbnailProvider&, AssetBrowser&)>;

        // 函数内 static 单例——不管调用方(各编辑器标签页 .cpp 里的静态 Registrar)和这个类
        // 自己的静态存储谁先完成初始化都没关系, 规避跨翻译单元的静态初始化顺序问题
        // (跟 ComponentPanelRegistry::get() 是同一个理由)。
        static AssetEditorRegistry& get();

        void registerFactory(Class* assetRootClass, Factory factory);

        // 没有为这个资产的 rootClass 注册过编辑器就什么都不做, 返回 false——不是所有资产
        // 类型都需要专属编辑器(比如 Mesh 目前就没有)。
        bool tryOpenEditor(Asset& asset, AssetEditorTabHost& tabHost, ThumbnailProvider& iconProvider, AssetBrowser& assetBrowser) const;

    private:
        std::unordered_map<Class*, Factory> mFactories;
    };

    template <typename AssetT, typename TabT>
    struct AssetEditorRegistrar
    {
        AssetEditorRegistrar()
        {
            AssetEditorRegistry::get().registerFactory(AssetT::staticClass(),
                [](Asset& asset, ThumbnailProvider& iconProvider, AssetBrowser& assetBrowser) -> std::unique_ptr<AssetEditorTab>
                {
                    return std::make_unique<TabT>(asset, iconProvider, assetBrowser);
                });
        }
    };
}
