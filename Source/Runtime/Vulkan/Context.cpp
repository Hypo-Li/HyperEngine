#ifdef __linux__
// 只在这里(以及 Swapchain.cpp, 真正调用 Xlib/XCB/Wayland surface 相关 Vulkan API 的两个
// 文件)局部打开这些宏, 换取 volk.h 展开 VkXlibSurfaceCreateInfoKHR 等类型声明和
// VK_KHR_XLIB_SURFACE_EXTENSION_NAME 等宏。见 ThirdParty/volk/CMakeLists.txt 里
// 为什么这些宏不再对所有消费者 PUBLIC 传播。
#define VK_USE_PLATFORM_XLIB_KHR
#define VK_USE_PLATFORM_WAYLAND_KHR
#ifdef HYP_HAS_XCB
#define VK_USE_PLATFORM_XCB_KHR
#endif
#endif

#include "Context.h"
#include "Device.h"

#include <Core/Common/LogSystem.h>
#include <cassert>
#include <cstring>

namespace hyp::vk
{
    static bool hasVkInstanceExtension(const char* name)
    {
        uint32_t extensionCount = 0;
        vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);

        std::vector<VkExtensionProperties> extensions(extensionCount);
        vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, extensions.data());

        for (const VkExtensionProperties& extension : extensions)
        {
            if (std::strcmp(extension.extensionName, name) == 0)
                return true;
        }
        return false;
    }

    static std::vector<const char*> getVkInstanceExtensions(
        const rhi::ContextDesc& contextDesc,
        bool enablePhysicalDeviceProperties2
    )
    {
        std::vector<const char*> extensions;

        if (enablePhysicalDeviceProperties2)
        {
            extensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
        }

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
        case rhi::NativeWindowType::Xcb:
        {
#ifdef VK_USE_PLATFORM_XCB_KHR
            extensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
            extensions.push_back(VK_KHR_XCB_SURFACE_EXTENSION_NAME);
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

    // deviceType 按优先级打分, 优先选独显; 找不到独显时(比如只有核显的笔记本, 或者
    // WSLg 下只暴露 Mesa llvmpipe 这种 PHYSICAL_DEVICE_TYPE_CPU 软件光栅化设备)回退到
    // 分数最高的可用设备, 而不是直接返回 VK_NULL_HANDLE——调用方并没有对空句柄做处理,
    // 静默返回空句柄会导致后续 vkGetPhysicalDeviceProperties(VK_NULL_HANDLE, ...) 直接崩溃。
    static int scorePhysicalDeviceType(VkPhysicalDeviceType type) noexcept
    {
        switch (type)
        {
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:   return 4;
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return 3;
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:    return 2;
        case VK_PHYSICAL_DEVICE_TYPE_CPU:            return 1;
        default:                                      return 0;
        }
    }

    static VkPhysicalDevice pickPhysicalDevice(VkInstance instance)
    {
        uint32_t physicalDeviceCount = 0;
        vkEnumeratePhysicalDevices(instance, &physicalDeviceCount, nullptr);
        std::vector<VkPhysicalDevice> physicalDevices(physicalDeviceCount);
        vkEnumeratePhysicalDevices(instance, &physicalDeviceCount, physicalDevices.data());

        VkPhysicalDevice result = VK_NULL_HANDLE;
        int bestScore = -1;
        for (const auto& physicalDevice : physicalDevices)
        {
            VkPhysicalDeviceProperties physicalDeviceProperties;
            vkGetPhysicalDeviceProperties(physicalDevice, &physicalDeviceProperties);
            int score = scorePhysicalDeviceType(physicalDeviceProperties.deviceType);
            if (score > bestScore)
            {
                bestScore = score;
                result = physicalDevice;
            }
        }
        assert(result != VK_NULL_HANDLE && "No Vulkan physical device found");
        VkPhysicalDeviceProperties finalPhysicalDeviceProperties;
        vkGetPhysicalDeviceProperties(result, &finalPhysicalDeviceProperties);
        LOG_INFO("Picked Vulkan physical device: {}", finalPhysicalDeviceProperties.deviceName);
        return result;
    }

    std::string vkVersionToString(uint32_t version)
    {
        return std::to_string(VK_VERSION_MAJOR(version)) + "." +
            std::to_string(VK_VERSION_MINOR(version)) + "." +
            std::to_string(VK_VERSION_PATCH(version));
    }

    Context::Context(const rhi::ContextDesc& desc) :
        rhi::Context(rhi::Backend::Vulkan)
    {
        assert(desc.backend == rhi::Backend::Vulkan);

        volkInitialize();

        uint32_t loaderApiVersion = VK_API_VERSION_1_0;
        if (vkEnumerateInstanceVersion != nullptr)
        {
            vkEnumerateInstanceVersion(&loaderApiVersion);
        }
#if 1
        const uint32_t appApiVersion = loaderApiVersion;
#else
        // for test
        const uint32_t appApiVersion = VK_API_VERSION_1_1;
#endif

        bool physicalDeviceProperties2 = appApiVersion >= VK_API_VERSION_1_1;
        if (!physicalDeviceProperties2)
        {
            physicalDeviceProperties2 =
                hasVkInstanceExtension(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
        }

        VkApplicationInfo appInfo = {};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = "HyperEngine";
        appInfo.applicationVersion = VK_MAKE_VERSION(0, 0, 0);
        appInfo.apiVersion = appApiVersion;
        
        LOG_INFO("Enabled Vulkan API Version: {}", vkVersionToString(appApiVersion));

        VkInstanceCreateInfo instanceInfo = {};
        instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        instanceInfo.pApplicationInfo = &appInfo;
        auto layers = getVkInstanceLayers(desc);
        instanceInfo.enabledLayerCount = static_cast<uint32_t>(layers.size());
        instanceInfo.ppEnabledLayerNames = layers.data();
        auto extensions = getVkInstanceExtensions(
            desc,
            appApiVersion < VK_API_VERSION_1_1 && physicalDeviceProperties2
        );
        instanceInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
        instanceInfo.ppEnabledExtensionNames = extensions.data();

// #define ENABLE_BEST_PRACTICES_VALIDATION
#define ENABLE_SYNCHRONIZATION_VALIDATION
//#define ENABLE_GPU_ASSISTED_VALIDATION

        std::vector<VkValidationFeatureEnableEXT> enabledValidationFeatures = {
#ifdef ENABLE_BEST_PRACTICES_VALIDATION
            VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT,
#endif
#ifdef ENABLE_SYNCHRONIZATION_VALIDATION
            VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT,
#endif
#ifdef ENABLE_GPU_ASSISTED_VALIDATION
            VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT
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

        mDevice = std::make_unique<Device>(
            desc,
            mInstance,
            mPhysicalDevice,
            appApiVersion,
            physicalDeviceProperties2
        );
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
