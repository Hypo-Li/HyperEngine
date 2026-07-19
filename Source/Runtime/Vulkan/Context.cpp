#include "Context.h"
#include "Device.h"

#include <Core/Common/LogSystem.h>

namespace hyp::vk
{
    static std::vector<const char*> getVkInstanceExtensions(const rhi::ContextDesc& contextDesc)
    {
        std::vector<const char*> extensions;

        if (contextDesc.enableValidation)
        {
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        }

        switch (contextDesc.swapchainDesc.nativeWindow.type)
        {
        case rhi::NativeWindowType::Win32:
        {
#ifdef VK_USE_PLATFORM_WIN32_KHR
            extensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
            extensions.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
#endif
            break;
        }
        case rhi::NativeWindowType::Xlib:
        {
#ifdef VK_USE_PLATFORM_XLIB_KHR
            extensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
            extensions.push_back(VK_KHR_XLIB_SURFACE_EXTENSION_NAME);
#endif
            break;
        }
        case rhi::NativeWindowType::Wayland:
        {
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
            extensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
            extensions.push_back(VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME);
#endif
            break;
        }
        default:
            break;
        }
        return extensions;
    }

    static std::vector<const char*> getVkInstanceLayers(const rhi::ContextDesc& contextDesc)
    {
        std::vector<const char*> layers;
        if (contextDesc.enableValidation)
        {
            layers.push_back("VK_LAYER_KHRONOS_validation");
        }
        return layers;
    }

    static VkPhysicalDevice pickPhysicalDevice(VkInstance instance)
    {
        uint32_t physicalDeviceCount = 0;
        vkEnumeratePhysicalDevices(instance, &physicalDeviceCount, nullptr);
        std::vector<VkPhysicalDevice> physicalDevices(physicalDeviceCount);
        vkEnumeratePhysicalDevices(instance, &physicalDeviceCount, physicalDevices.data());

        VkPhysicalDevice result = VK_NULL_HANDLE;
        for (const auto& physicalDevice : physicalDevices)
        {
            VkPhysicalDeviceProperties physicalDeviceProperties;
            vkGetPhysicalDeviceProperties(physicalDevice, &physicalDeviceProperties);
            VkPhysicalDeviceFeatures physicalDeviceFeatures;
            vkGetPhysicalDeviceFeatures(physicalDevice, &physicalDeviceFeatures);
            if (physicalDeviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
            {
                result = physicalDevice;
                break;
            }
        }
        return result;
    }

    Context::Context(const rhi::ContextDesc& desc) :
        rhi::Context(rhi::Backend::Vulkan)
    {
        assert(desc.backend == rhi::Backend::Vulkan);

        volkInitialize();

        uint32_t appApiVersion = VK_API_VERSION_1_0;
        if (vkEnumerateInstanceVersion != nullptr)
        {
            vkEnumerateInstanceVersion(&appApiVersion);
        }

        VkApplicationInfo appInfo = {};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = "HyperEngine";
        appInfo.applicationVersion = VK_MAKE_VERSION(0, 0, 0);
        appInfo.apiVersion = appApiVersion;

        VkInstanceCreateInfo instanceInfo = {};
        instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        instanceInfo.pApplicationInfo = &appInfo;
        auto layers = getVkInstanceLayers(desc);
        instanceInfo.enabledLayerCount = static_cast<uint32_t>(layers.size());
        instanceInfo.ppEnabledLayerNames = layers.data();
        auto extensions = getVkInstanceExtensions(desc);
        instanceInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
        instanceInfo.ppEnabledExtensionNames = extensions.data();

// #define ENABLE_BEST_PRACTICES_VALIDATION
#define ENABLE_SYNCHRONIZATION_VALIDATION

        std::vector<VkValidationFeatureEnableEXT> enabledValidationFeatures = {
#ifdef ENABLE_BEST_PRACTICES_VALIDATION
            VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT,
#endif
#ifdef ENABLE_SYNCHRONIZATION_VALIDATION
            VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT
#endif
        };
        VkValidationFeaturesEXT validationFeatures = {};
        validationFeatures.sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT;
        validationFeatures.enabledValidationFeatureCount = static_cast<uint32_t>(enabledValidationFeatures.size());
        validationFeatures.pEnabledValidationFeatures = enabledValidationFeatures.data();

        if (desc.enableValidation && !enabledValidationFeatures.empty())
            instanceInfo.pNext = &validationFeatures;

        if (vkCreateInstance(&instanceInfo, nullptr, &mInstance) != VK_SUCCESS)
        {
            LOG_ERROR("Failed to create vulkan instance");
        }

        volkLoadInstance(mInstance);

        if (desc.enableValidation)
        {
            createDebugUtilsMessenger();
        }

        mPhysicalDevice = pickPhysicalDevice(mInstance);

        mDevice = std::make_unique<Device>(desc, mInstance, mPhysicalDevice);
    }

    Context::~Context()
    {
        mDevice.reset();

        if (mDebugMessenger != VK_NULL_HANDLE)
        {
            vkDestroyDebugUtilsMessengerEXT(mInstance, mDebugMessenger, nullptr);
        }
        vkDestroyInstance(mInstance, nullptr);

        volkFinalize();
    }

    static VkBool32 debugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
        VkDebugUtilsMessageTypeFlagsEXT messageType,
        const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
        void* pUserData)
    {
        switch (messageSeverity)
        {
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
            LOG_INFO("Validation layer: {}", pCallbackData->pMessage);
            break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
            LOG_WARN("Validation layer: {}", pCallbackData->pMessage);
            break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
            LOG_ERROR("Validation layer: {}", pCallbackData->pMessage);
            break;
        default:
            break;
        }
        return VK_FALSE;
    }

    void Context::createDebugUtilsMessenger()
    {
        VkDebugUtilsMessengerCreateInfoEXT createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        createInfo.messageSeverity =
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        createInfo.messageType =
            VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        createInfo.pfnUserCallback = debugCallback;
        createInfo.pUserData = nullptr;

        if (vkCreateDebugUtilsMessengerEXT(mInstance, &createInfo, nullptr, &mDebugMessenger) != VK_SUCCESS)
        {
            LOG_ERROR("Failed to create debug utils messenger.");
        }
    }

}
