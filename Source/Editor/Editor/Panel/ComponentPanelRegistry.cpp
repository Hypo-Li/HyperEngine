#include "ComponentPanelRegistry.h"
#include "DefaultComponentPanel.h"

#include <Core/Reflection/Class.h>

namespace hyp::ed
{
    ComponentPanelRegistry& ComponentPanelRegistry::get()
    {
        static ComponentPanelRegistry instance;
        return instance;
    }

    void ComponentPanelRegistry::registerFactory(Class* componentClass, Factory factory)
    {
        mFactories[componentClass] = std::move(factory);
    }

    std::unique_ptr<IComponentPanel> ComponentPanelRegistry::createPanel(Component& component, UndoStack& undoStack, ThumbnailProvider& iconProvider, AssetBrowser& assetBrowser) const
    {
        auto it = mFactories.find(component.getClass());
        std::unique_ptr<IComponentPanel> panel = it != mFactories.end()
            ? it->second(component)
            : std::make_unique<DefaultComponentPanel>(component);

        panel->injectDependencies(undoStack, iconProvider, assetBrowser);
        return panel;
    }
}
