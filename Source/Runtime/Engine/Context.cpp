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
        mRenderTagRegistry = nullptr;
        mAssetManager = nullptr;
        mRendererContext = nullptr;
        mRenderFrontend = nullptr;
        mSampleStateManager = nullptr;
        mInputSystem = nullptr;
    }
}
