#pragma once

#include "ComponentPanel.h"

#include <functional>
#include <memory>
#include <unordered_map>

namespace hyp
{
    class Class;
}

namespace hyp::ed
{
    // Class -> IComponentPanel 工厂的注册表。各具体 Component 的面板 .cpp 通过
    // ComponentPanelRegistrar<T, PanelT> 的静态实例在自己的翻译单元初始化时挂进来,
    // 不需要在这个类或 Inspector 里为每种 Component 手写分支。
    class ComponentPanelRegistry
    {
    public:
        using Factory = std::function<std::unique_ptr<IComponentPanel>(Component&)>;

        // 函数内 static 单例——不管调用方(各 Panel .cpp 里的静态 Registrar)和这个类
        // 自己的静态存储谁先完成初始化都没关系, 规避跨翻译单元的静态初始化顺序问题。
        static ComponentPanelRegistry& get();

        void registerFactory(Class* componentClass, Factory factory);

        // 按 component.getClass() 精确匹配; 没有为这个具体 Class 注册过面板就返回
        // DefaultComponentPanel, 不会沿着基类链往上找——基类面板通常画不出派生类
        // 新增的字段, 与其展示一个看似能用实则字段缺失的面板, 不如老实显示"无自定义面板"。
        //
        // undoStack/iconProvider/assetBrowser 不经过各个 PanelT 自己的构造函数传入(那样每个
        // 面板都要重复声明、转发这几个参数), 而是构造完成后通过
        // IComponentPanel::injectDependencies() 直接注入——PanelT 完全不需要知道这件事,
        // 只要调 getUndoStack()/getIconProvider()/getAssetBrowser() 就行。
        std::unique_ptr<IComponentPanel> createPanel(Component& component, UndoStack& undoStack, ThumbnailProvider& iconProvider, AssetBrowser& assetBrowser) const;

    private:
        std::unordered_map<Class*, Factory> mFactories;
    };

    template <typename ComponentT, typename PanelT>
    struct ComponentPanelRegistrar
    {
        ComponentPanelRegistrar()
        {
            ComponentPanelRegistry::get().registerFactory(ComponentT::staticClass(),
                [](Component& component) -> std::unique_ptr<IComponentPanel>
                {
                    return std::make_unique<PanelT>(static_cast<ComponentT&>(component));
                });
        }
    };
}
