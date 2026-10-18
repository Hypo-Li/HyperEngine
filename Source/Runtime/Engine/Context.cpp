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

    Context::~Context()
    {
        mPhysicsSystem = nullptr;
        mRenderTagRegistry = nullptr;
        mAssetManager = nullptr;
        mRendererContext = nullptr;
        mRenderFrontend = nullptr;
        mInputSystem = nullptr;
    }
}
