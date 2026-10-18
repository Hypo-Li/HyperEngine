#pragma once
#include <vector>

namespace hyp
{
    namespace rhi
    {
        class CommandList;
    }

    class ResourceProxy;

    class PrepareQueue
    {
    public:
        void enqueue(ResourceProxy* proxy);

        void remove(ResourceProxy* proxy);

        void flush(rhi::CommandList& cmdList);

    private:
        std::vector<ResourceProxy*> mProxies;
    };
}
