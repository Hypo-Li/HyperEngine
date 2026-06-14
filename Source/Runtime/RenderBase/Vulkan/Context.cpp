#include "Context.h"

namespace hyp::vk
{
    Context& Context::get()
    {
        static Context vulkanContext;
        return vulkanContext;
    }

#ifdef NDEBUG
    constexpr bool kEnableValidationLayers = false;
#else
    constexpr bool kEnableValidationLayers = true;
#endif

    Context::Context()
    {
        std::vector<const char*> instanceExtensions;
        std::vector<const char*> instanceLayers;

        instanceExtensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
        instanceExtensions.push_back("VK_KHR_win32_surface");

        if (kEnableValidationLayers)
        {
            instanceExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
            instanceLayers.push_back("VK_LAYER_KHRONOS_validation");
        }

        mInstance = new Instance(instanceExtensions, instanceLayers);

        mPhysicalDevice = new PhysicalDevice(*mInstance, mInstance->pickPhysicalDevice());

        const char* deviceExtensions[] = {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME,
            VK_EXT_SHADER_ATOMIC_FLOAT_EXTENSION_NAME,
            VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME,
            VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
            VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
            VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
            // VK_EXT_MESH_SHADER_EXTENSION_NAME
        };
        VkPhysicalDeviceFeatures features = {};
        features.fragmentStoresAndAtomics = VK_TRUE;
        features.samplerAnisotropy = VK_TRUE;
        features.independentBlend = VK_TRUE;
        features.dualSrcBlend = VK_TRUE;
        mDevice = new Device(*mPhysicalDevice, deviceExtensions, features);

    }

    Context::~Context()
    {
        mDevice = nullptr;
        mPhysicalDevice = nullptr;
        mInstance = nullptr;
    }
}
