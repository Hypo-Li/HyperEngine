#include "Context.h"

namespace hyp
{
    Context& Context::get()
    {
        static Context instance;
        return instance;
    }

    Context::Context()
    {
        mEnvironment = Environment::Standalone;
    }
}
