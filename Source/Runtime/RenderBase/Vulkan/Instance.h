#pragma once
#include <vector>
#include <span>
#include <unordered_set>
#include <unordered_map>
#include <assert.h>

#define VK_NO_PROTOTYPES
#include <volk.h>

#include <Core/Common/Refable.h>
#include <Core/Common/LogSystem.h>

namespace hyp::vk
{
    class Instance : public Refable
    {
    public:
        Instance(std::span<const char*> extensions, std::span<const char*> layers);
        ~Instance();

        Instance(const Instance&) = delete;
        Instance& operator=(const Instance&) = delete;

        VkInstance getHandle() const
        {
            return mInstance;
        }

        VkPhysicalDevice pickPhysicalDevice() const;

    private:
        void createDebugUtilsMessenger();

        void destroyDebugUtilsMessenger();

        static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
            VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
            VkDebugUtilsMessageTypeFlagsEXT messageType,
            const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
            void* pUserData
        );

        static bool isPhysicalDeviceSuitable(VkPhysicalDevice physicalDevice);

    private:
        VkInstance mInstance{ VK_NULL_HANDLE };
        VkDebugUtilsMessengerEXT mDebugMessenger{ VK_NULL_HANDLE };
    };
}
