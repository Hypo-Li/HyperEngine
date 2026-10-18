#pragma once
#include <memory>
#include <typeindex>
#include <unordered_map>

namespace hyp
{
    // 给"接口定义在下层模块、具体实现在上层模块"这类工具提供统一的注册/查找入口——每种工具
    // 各自开一个纯抽象接口(比如 IShaderCompiler), 具体实现物理上住在更上层的库里, 下层库不能
    // 反过来依赖它。组装根(Editor/各 Test 的 main())在构造 Engine 之前调用一次
    // registerService() 完成绑定, 下层代码按需调用 getService() 取得指针。未注册时
    // getService() 返回 nullptr, 是合法状态, 不是错误——通常意味着当前构建配置没有链接对应
    // 的上层实现。
    //
    // 只应在程序启动阶段(单线程)完成所有 registerService() 调用, 此后各线程并发调用
    // getService() 是安全的(只读, 不再有并发写入)。
    //
    // 内部用 shared_ptr<void> 做类型擦除存储(不是 std::any——any 要求持有的类型可拷贝构造,
    // 存不了 move-only 的 unique_ptr<TService>)。shared_ptr<void> 的控制块记住了构造时的
    // 具体类型, 析构时依然会调到正确的类型析构函数, 生命周期语义和直接存 unique_ptr 一致。
    class ModuleServiceRegistry
    {
    public:
        static ModuleServiceRegistry& get();

        ModuleServiceRegistry(const ModuleServiceRegistry&) = delete;
        ModuleServiceRegistry& operator=(const ModuleServiceRegistry&) = delete;

        template <typename TService>
        void registerService(std::unique_ptr<TService> service)
        {
            mServices[std::type_index(typeid(TService))] = std::shared_ptr<void>(std::move(service));
        }

        template <typename TService>
        TService* getService()
        {
            auto it = mServices.find(std::type_index(typeid(TService)));
            if (it == mServices.end())
                return nullptr;
            return static_cast<TService*>(it->second.get());
        }

    private:
        ModuleServiceRegistry() = default;

        std::unordered_map<std::type_index, std::shared_ptr<void>> mServices;
    };
}
