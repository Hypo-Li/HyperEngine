#pragma once
#include <Core/Object/Object.h>

#include <span>

namespace hyp
{
    class MaterialTemplate;

    // 一段可复用的 Slang 源码, 被引入后原样拼进最终 shader 源码——不解析, 不做符号内省
    class MaterialFunctionLibrary : public Object
    {
        REFLECT_CLASS(MaterialFunctionLibrary)
        friend class MaterialTemplate;
    public:
        MaterialFunctionLibrary() = default;
        ~MaterialFunctionLibrary();

        void setLibrarySource(std::string librarySource) noexcept;

        const std::string& getLibrarySource() const noexcept
        {
            return mLibrarySource;
        }

        // 失败(library 为空/是自己/已经引入过/会成环)时返回 false, 不做任何改动
        bool addImportedLibrary(MaterialFunctionLibrary* library);

        void removeImportedLibrary(MaterialFunctionLibrary* library);

        std::span<const TRef<MaterialFunctionLibrary>> getImportedLibraries() const noexcept
        {
            return mImportedLibraries;
        }

        // addImportedLibrary(candidate) 现在调用会不会成功——供编辑器在资产选择框里预先
        // 排除掉选了就会失败的候选项用, addImportedLibrary() 内部也是靠这个判断
        bool canImportLibrary(const MaterialFunctionLibrary* candidate) const noexcept;

    private:
        // candidate 现在能不能(直接或间接)到达 this —— 能的话 this 再引入 candidate 就会成环,
        // 跟 Transform::setParent() 检查 newParent 是不是自身后代是同一个套路
        bool wouldCreateCycle(const MaterialFunctionLibrary* candidate) const noexcept;

        void notifyDependents();

        void registerDependent(MaterialFunctionLibrary* library);
        void unregisterDependent(MaterialFunctionLibrary* library);
        void registerDependent(MaterialTemplate* materialTemplate);
        void unregisterDependent(MaterialTemplate* materialTemplate);

        std::string mLibrarySource;
        std::vector<TRef<MaterialFunctionLibrary>> mImportedLibraries; // 正向边: 我要用到的库, 持有引用
        std::vector<MaterialFunctionLibrary*> mDependentLibraries;     // 反向边: 引入了我的库, 不持有
        std::vector<MaterialTemplate*> mDependentTemplates;            // 反向边: 引用了我的模板, 不持有
    };

    template <> Type* Reflection::createType<MaterialFunctionLibrary>();

    // 从若干根库出发, 按"依赖排在前面"的顺序收集所有直接/间接引入到的库源码拼接成一段文本,
    // 同一个库被多条路径引入到只会出现一次(出现在它作为依赖第一次被处理完的位置)
    std::string resolveMaterialFunctionLibrarySources(std::span<const TRef<MaterialFunctionLibrary>> roots);
}
