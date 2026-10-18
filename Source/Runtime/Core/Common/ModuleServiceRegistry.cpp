#include "ModuleServiceRegistry.h"

namespace hyp
{
    ModuleServiceRegistry& ModuleServiceRegistry::get()
    {
        static ModuleServiceRegistry instance;
        return instance;
    }
}
