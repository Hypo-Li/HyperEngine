#include "MaterialFunctionLibrary.h"
#include "MaterialTemplate.h"

#include <algorithm>
#include <functional>
#include <unordered_set>

namespace hyp
{
    REGISTER_CLASS(MaterialFunctionLibrary)

    MaterialFunctionLibrary::~MaterialFunctionLibrary()
    {
        assert(mDependentLibraries.empty() && mDependentTemplates.empty());
        for (const TRef<MaterialFunctionLibrary>& imported : mImportedLibraries)
            imported.get()->unregisterDependent(this);
    }

    void MaterialFunctionLibrary::setLibrarySource(std::string librarySource) noexcept
    {
        mLibrarySource = std::move(librarySource);
        notifyDependents();
    }

    bool MaterialFunctionLibrary::addImportedLibrary(MaterialFunctionLibrary* library)
    {
        if (!canImportLibrary(library))
            return false;

        mImportedLibraries.emplace_back(library);
        library->registerDependent(this);
        notifyDependents();
        return true;
    }

    void MaterialFunctionLibrary::removeImportedLibrary(MaterialFunctionLibrary* library)
    {
        auto it = std::find(mImportedLibraries.begin(), mImportedLibraries.end(), library);
        if (it == mImportedLibraries.end())
            return;

        library->unregisterDependent(this);
        mImportedLibraries.erase(it);
        notifyDependents();
    }

    bool MaterialFunctionLibrary::canImportLibrary(const MaterialFunctionLibrary* candidate) const noexcept
    {
        if (!candidate || candidate == this)
            return false;
        if (std::find(mImportedLibraries.begin(), mImportedLibraries.end(), candidate) != mImportedLibraries.end())
            return false;
        return !wouldCreateCycle(candidate);
    }

    bool MaterialFunctionLibrary::wouldCreateCycle(const MaterialFunctionLibrary* candidate) const noexcept
    {
        std::unordered_set<const MaterialFunctionLibrary*> visited;
        std::vector<const MaterialFunctionLibrary*> pending{ candidate };
        while (!pending.empty())
        {
            const MaterialFunctionLibrary* current = pending.back();
            pending.pop_back();
            if (current == this)
                return true;
            if (!visited.insert(current).second)
                continue;
            for (const TRef<MaterialFunctionLibrary>& imported : current->mImportedLibraries)
                pending.push_back(imported.get());
        }
        return false;
    }

    void MaterialFunctionLibrary::notifyDependents()
    {
        for (MaterialFunctionLibrary* library : mDependentLibraries)
            library->notifyDependents();
        for (MaterialTemplate* materialTemplate : mDependentTemplates)
            materialTemplate->onFunctionLibraryUpdated();
    }

    void MaterialFunctionLibrary::registerDependent(MaterialFunctionLibrary* library)
    {
        mDependentLibraries.push_back(library);
    }

    void MaterialFunctionLibrary::unregisterDependent(MaterialFunctionLibrary* library)
    {
        mDependentLibraries.erase(std::remove(mDependentLibraries.begin(), mDependentLibraries.end(), library), mDependentLibraries.end());
    }

    void MaterialFunctionLibrary::registerDependent(MaterialTemplate* materialTemplate)
    {
        mDependentTemplates.push_back(materialTemplate);
    }

    void MaterialFunctionLibrary::unregisterDependent(MaterialTemplate* materialTemplate)
    {
        mDependentTemplates.erase(std::remove(mDependentTemplates.begin(), mDependentTemplates.end(), materialTemplate), mDependentTemplates.end());
    }

    std::string resolveMaterialFunctionLibrarySources(std::span<const TRef<MaterialFunctionLibrary>> roots)
    {
        std::vector<MaterialFunctionLibrary*> ordered;
        std::unordered_set<MaterialFunctionLibrary*> visited;

        std::function<void(MaterialFunctionLibrary*)> visit = [&](MaterialFunctionLibrary* library)
        {
            if (!library || !visited.insert(library).second)
                return;
            for (const TRef<MaterialFunctionLibrary>& imported : library->getImportedLibraries())
                visit(imported.get());
            ordered.push_back(library);
        };

        for (const TRef<MaterialFunctionLibrary>& root : roots)
            visit(root.get());

        std::string result;
        for (MaterialFunctionLibrary* library : ordered)
            result += library->getLibrarySource();
        return result;
    }

    template <> Type* Reflection::createType<MaterialFunctionLibrary>()
    {
        auto c = new TClass<MaterialFunctionLibrary, Object>("MaterialFunctionLibrary");
        c->setDisplayName("Material Function Library");
        c->addProperty("Library Source", &MaterialFunctionLibrary::mLibrarySource);
        c->addProperty("Imported Libraries", &MaterialFunctionLibrary::mImportedLibraries);
        return c;
    }
}
