#include "vulkan_util.h"
#include "enumerate.h"
#include "error.h"
#include "logging.h"

using namespace std::string_literals;

#if PLATFORM_COMPILER_GCC
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
#endif
#include "SPIRV/GLSL.std.450.h"
#include "SPIRV/GlslangToSpv.h"
#include "glslang/Public/ShaderLang.h"
#if PLATFORM_COMPILER_GCC
#pragma GCC diagnostic pop
#endif

#include <fstream>

tl::expected<VkInstance, std::system_error>
iris::Renderer::CreateInstance(gsl::czstring<> appName, std::uint32_t appVersion,
               gsl::span<gsl::czstring<>> extensionNames,
               gsl::span<gsl::czstring<>> layerNames,
               PFN_vkDebugUtilsMessengerCallbackEXT debugUtilsMessengerCallback) noexcept {
  IRIS_LOG_ENTER();

  std::uint32_t instanceVersion;
  vkEnumerateInstanceVersion(&instanceVersion); // can only return VK_SUCCESS

  GetLogger()->debug(
    "Vulkan Instance Version: {}.{}.{}", VK_VERSION_MAJOR(instanceVersion),
    VK_VERSION_MINOR(instanceVersion), VK_VERSION_PATCH(instanceVersion));

  // Get the number of instance extension properties.
  std::uint32_t numExtensionProperties;
  if (auto result = vkEnumerateInstanceExtensionProperties(
        nullptr, &numExtensionProperties, nullptr);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(
      std::system_error(make_error_code(result),
                        "Cannot enumerate instance extension properties"));
  }

  // Get the instance extension properties.
  absl::FixedArray<VkExtensionProperties> extensionProperties(
    numExtensionProperties);
  if (auto result = vkEnumerateInstanceExtensionProperties(
        nullptr, &numExtensionProperties, extensionProperties.data());
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(
      std::system_error(make_error_code(result),
                        "Cannot enumerate instance extension properties"));
  }

  VkApplicationInfo ai = {};
  ai.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  ai.pApplicationName = appName;
  ai.applicationVersion = appVersion;
  ai.pEngineName = "iris";
  ai.engineVersion =
    VK_MAKE_VERSION(kVersionMajor, kVersionMinor, kVersionPatch);

  VkInstanceCreateInfo ci = {};
  ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  ci.pApplicationInfo = &ai;
  ci.enabledLayerCount = gsl::narrow_cast<std::uint32_t>(layerNames.size());
  ci.ppEnabledLayerNames = layerNames.data();
  ci.enabledExtensionCount =
    gsl::narrow_cast<std::uint32_t>(extensionNames.size());
  ci.ppEnabledExtensionNames = extensionNames.data();

  VkDebugUtilsMessengerCreateInfoEXT dumci = {};
  dumci.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
  dumci.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                          VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT |
                          VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                          VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT;
  dumci.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                      VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                      VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
  dumci.pfnUserCallback = debugUtilsMessengerCallback;

  if (debugUtilsMessengerCallback) ci.pNext = &dumci;

  VkInstance instance;
  if (auto result = vkCreateInstance(&ci, nullptr, &instance);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(
      std::system_error(make_error_code(result), "Cannot create instance"));
  }

  Ensures(instance != VK_NULL_HANDLE);
  IRIS_LOG_LEAVE();
  return instance;
} // iris::Renderer::CreateInstance

tl::expected<VkDebugUtilsMessengerEXT, std::system_error>
iris::Renderer::CreateDebugUtilsMessenger(
  VkInstance instance,
  PFN_vkDebugUtilsMessengerCallbackEXT debugUtilsMessengerCallback) noexcept {
  IRIS_LOG_ENTER();
  Expects(instance != VK_NULL_HANDLE);

  VkDebugUtilsMessengerCreateInfoEXT dumci = {};
  dumci.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
  dumci.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                          VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT |
                          VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                          VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT;
  dumci.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                      VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                      VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
  dumci.pfnUserCallback = debugUtilsMessengerCallback;

  VkDebugUtilsMessengerEXT messenger;
  if (auto result =
        vkCreateDebugUtilsMessengerEXT(instance, &dumci, nullptr, &messenger);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(
      make_error_code(result), "Cannot create debug utils messenger"));
  }

  Ensures(messenger != VK_NULL_HANDLE);
  IRIS_LOG_LEAVE();
  return messenger;
} // iris::Renderer::CreateDebugUtilsMessenger

bool iris::Renderer::ComparePhysicalDeviceFeatures(
  VkPhysicalDeviceFeatures2 a, VkPhysicalDeviceFeatures2 b) noexcept {
  bool result = false;
  result |= (a.features.robustBufferAccess == b.features.robustBufferAccess);
  result |= (a.features.fullDrawIndexUint32 == b.features.fullDrawIndexUint32);
  result |= (a.features.imageCubeArray == b.features.imageCubeArray);
  result |= (a.features.independentBlend == b.features.independentBlend);
  result |= (a.features.geometryShader == b.features.geometryShader);
  result |= (a.features.tessellationShader == b.features.tessellationShader);
  result |= (a.features.sampleRateShading == b.features.sampleRateShading);
  result |= (a.features.dualSrcBlend == b.features.dualSrcBlend);
  result |= (a.features.logicOp == b.features.logicOp);
  result |= (a.features.multiDrawIndirect == b.features.multiDrawIndirect);
  result |= (a.features.drawIndirectFirstInstance ==
             b.features.drawIndirectFirstInstance);
  result |= (a.features.depthClamp == b.features.depthClamp);
  result |= (a.features.depthBiasClamp == b.features.depthBiasClamp);
  result |= (a.features.fillModeNonSolid == b.features.fillModeNonSolid);
  result |= (a.features.depthBounds == b.features.depthBounds);
  result |= (a.features.wideLines == b.features.wideLines);
  result |= (a.features.largePoints == b.features.largePoints);
  result |= (a.features.alphaToOne == b.features.alphaToOne);
  result |= (a.features.multiViewport == b.features.multiViewport);
  result |= (a.features.samplerAnisotropy == b.features.samplerAnisotropy);
  result |=
    (a.features.textureCompressionETC2 == b.features.textureCompressionETC2);
  result |= (a.features.textureCompressionASTC_LDR ==
             b.features.textureCompressionASTC_LDR);
  result |=
    (a.features.textureCompressionBC == b.features.textureCompressionBC);
  result |=
    (a.features.occlusionQueryPrecise == b.features.occlusionQueryPrecise);
  result |=
    (a.features.pipelineStatisticsQuery == b.features.pipelineStatisticsQuery);
  result |= (a.features.vertexPipelineStoresAndAtomics ==
             b.features.vertexPipelineStoresAndAtomics);
  result |= (a.features.fragmentStoresAndAtomics ==
             b.features.fragmentStoresAndAtomics);
  result |= (a.features.shaderTessellationAndGeometryPointSize ==
             b.features.shaderTessellationAndGeometryPointSize);
  result |= (a.features.shaderImageGatherExtended ==
             b.features.shaderImageGatherExtended);
  result |= (a.features.shaderStorageImageExtendedFormats ==
             b.features.shaderStorageImageExtendedFormats);
  result |= (a.features.shaderStorageImageMultisample ==
             b.features.shaderStorageImageMultisample);
  result |= (a.features.shaderStorageImageReadWithoutFormat ==
             b.features.shaderStorageImageReadWithoutFormat);
  result |= (a.features.shaderStorageImageWriteWithoutFormat ==
             b.features.shaderStorageImageWriteWithoutFormat);
  result |= (a.features.shaderUniformBufferArrayDynamicIndexing ==
             b.features.shaderUniformBufferArrayDynamicIndexing);
  result |= (a.features.shaderSampledImageArrayDynamicIndexing ==
             b.features.shaderSampledImageArrayDynamicIndexing);
  result |= (a.features.shaderStorageBufferArrayDynamicIndexing ==
             b.features.shaderStorageBufferArrayDynamicIndexing);
  result |= (a.features.shaderStorageImageArrayDynamicIndexing ==
             b.features.shaderStorageImageArrayDynamicIndexing);
  result |= (a.features.shaderClipDistance == b.features.shaderClipDistance);
  result |= (a.features.shaderCullDistance == b.features.shaderCullDistance);
  result |= (a.features.shaderFloat64 == b.features.shaderFloat64);
  result |= (a.features.shaderInt64 == b.features.shaderInt64);
  result |= (a.features.shaderInt16 == b.features.shaderInt16);
  result |=
    (a.features.shaderResourceResidency == b.features.shaderResourceResidency);
  result |=
    (a.features.shaderResourceMinLod == b.features.shaderResourceMinLod);
  result |= (a.features.sparseBinding == b.features.sparseBinding);
  result |=
    (a.features.sparseResidencyBuffer == b.features.sparseResidencyBuffer);
  result |=
    (a.features.sparseResidencyImage2D == b.features.sparseResidencyImage2D);
  result |=
    (a.features.sparseResidencyImage3D == b.features.sparseResidencyImage3D);
  result |=
    (a.features.sparseResidency2Samples == b.features.sparseResidency2Samples);
  result |=
    (a.features.sparseResidency4Samples == b.features.sparseResidency4Samples);
  result |=
    (a.features.sparseResidency8Samples == b.features.sparseResidency8Samples);
  result |= (a.features.sparseResidency16Samples ==
             b.features.sparseResidency16Samples);
  result |=
    (a.features.sparseResidencyAliased == b.features.sparseResidencyAliased);
  result |=
    (a.features.variableMultisampleRate == b.features.variableMultisampleRate);
  result |= (a.features.inheritedQueries == b.features.inheritedQueries);
  return result;
} // iris::Renderer::ComparePhysicalDeviceFeatures

tl::expected<std::uint32_t, std::system_error>
iris::Renderer::GetQueueFamilyIndex(VkPhysicalDevice physicalDevice,
                                    VkQueueFlags queueFlags) noexcept {
  IRIS_LOG_ENTER();
  Expects(physicalDevice != VK_NULL_HANDLE);

  // Get the number of physical device queue family properties
  std::uint32_t numQueueFamilyProperties;
  vkGetPhysicalDeviceQueueFamilyProperties2(physicalDevice,
                                            &numQueueFamilyProperties, nullptr);

  // Get the physical device queue family properties
  absl::FixedArray<VkQueueFamilyProperties2> queueFamilyProperties(
    numQueueFamilyProperties);
  for (auto& property : queueFamilyProperties) {
    property.sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2;
    property.pNext = nullptr;
  }

  vkGetPhysicalDeviceQueueFamilyProperties2(
    physicalDevice, &numQueueFamilyProperties, queueFamilyProperties.data());

  for (auto [i, props] : enumerate(queueFamilyProperties)) {
    auto&& qfProps = props.queueFamilyProperties;
    if (qfProps.queueCount == 0) continue;

    if (qfProps.queueFlags & queueFlags) {
      IRIS_LOG_LEAVE();
      return i;
    }
  }

  IRIS_LOG_LEAVE();
  return UINT32_MAX;
} // iris::Renderer::GetQueueFamilyIndex

tl::expected<bool, std::system_error> iris::Renderer::IsPhysicalDeviceGood(
  VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures2 features,
  gsl::span<gsl::czstring<>> extensionNames, VkQueueFlags queueFlags) noexcept {
  IRIS_LOG_ENTER();
  Expects(physicalDevice != VK_NULL_HANDLE);

  //
  // Get the properties.
  //

  VkPhysicalDeviceMultiviewProperties multiviewProps = {};
  multiviewProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_PROPERTIES;

  VkPhysicalDeviceMaintenance3Properties maint3Props = {};
  maint3Props.sType =
    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_3_PROPERTIES;
  maint3Props.pNext = &multiviewProps;

  VkPhysicalDeviceProperties2 physicalDeviceProperties = {};
  physicalDeviceProperties.sType =
    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
  physicalDeviceProperties.pNext = &maint3Props;

  vkGetPhysicalDeviceProperties2(physicalDevice, &physicalDeviceProperties);

  //
  // Get the features.
  //

  VkPhysicalDeviceFeatures2 physicalDeviceFeatures = {};
  physicalDeviceFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;

  vkGetPhysicalDeviceFeatures2(physicalDevice, &physicalDeviceFeatures);

  //
  // Get the extension properties.
  //

  // Get the number of physical device extension properties.
  std::uint32_t numExtensionProperties;
  if (auto result = vkEnumerateDeviceExtensionProperties(
        physicalDevice, nullptr, &numExtensionProperties, nullptr);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(
      make_error_code(result),
      "Cannot enumerate physical device extension properties"));
  }

  // Get the physical device extension properties.
  absl::FixedArray<VkExtensionProperties> extensionProperties(
    numExtensionProperties);
  if (auto result = vkEnumerateDeviceExtensionProperties(
        physicalDevice, nullptr, &numExtensionProperties,
        extensionProperties.data());
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(
      make_error_code(result),
      "Cannot enumerate physical device extension properties"));
  }

  std::uint32_t queueFamilyIndex;
  if (auto result = GetQueueFamilyIndex(physicalDevice, queueFlags)) {
    queueFamilyIndex = std::move(*result);
  } else {
    return tl::unexpected(result.error());
  }

  //
  // Check all queried data to see if this device is good.
  //

  // Check for any required features
  if (!ComparePhysicalDeviceFeatures(physicalDeviceFeatures, features)) {
    IRIS_LOG_LEAVE();
    return false;
  }

  // Check for each required extension
  for (auto&& required : extensionNames) {
    bool found = false;

    for (auto&& property : extensionProperties) {
      if (std::strcmp(required, property.extensionName) == 0) {
        found = true;
        break;
      }
    }

    if (!found) {
      IRIS_LOG_LEAVE();
      return false;
    }
  }

  // Check for the queue
  if (queueFamilyIndex == UINT32_MAX) {
    IRIS_LOG_LEAVE();
    return false;
  }

  IRIS_LOG_LEAVE();
  return true;
} // iris::Renderer::IsPhysicalDeviceGood

tl::expected<VkPhysicalDevice, std::system_error>
iris::Renderer::ChoosePhysicalDevice(VkInstance instance,
                                     VkPhysicalDeviceFeatures2 features,
                                     gsl::span<gsl::czstring<>> extensionNames,
                                     VkQueueFlags queueFlags) noexcept {
  IRIS_LOG_ENTER();
  Expects(instance != VK_NULL_HANDLE);

  // Get the number of physical devices present on the system
  std::uint32_t numPhysicalDevices;
  if (auto result =
        vkEnumeratePhysicalDevices(instance, &numPhysicalDevices, nullptr);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(
      make_error_code(result), "Cannot enumerate physical devices"));
  }

  // Get the physical devices present on the system
  absl::FixedArray<VkPhysicalDevice> physicalDevices(numPhysicalDevices);
  if (auto result = vkEnumeratePhysicalDevices(instance, &numPhysicalDevices,
                                               physicalDevices.data());
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(
      make_error_code(result), "Cannot enumerate physical devices"));
  }

  // Iterate through each physical device to find one that we can use.
  for (auto&& physicalDevice : physicalDevices) {
    if (auto good = IsPhysicalDeviceGood(physicalDevice, features,
                                         extensionNames, queueFlags)) {
      Ensures(physicalDevice != VK_NULL_HANDLE);
      IRIS_LOG_LEAVE();
      return physicalDevice;
    }
  }

  IRIS_LOG_LEAVE();
  return tl::unexpected(std::system_error(Error::kNoPhysicalDevice,
                                          "No suitable physical device found"));
} // iris::Renderer::ChoosePhysicalDevice

tl::expected<std::pair<VkDevice, std::uint32_t>, std::system_error>
iris::Renderer::CreateDevice(VkPhysicalDevice physicalDevice,
                             VkPhysicalDeviceFeatures2 physicalDeviceFeatures,
                             gsl::span<gsl::czstring<>> extensionNames,
                             std::uint32_t queueFamilyIndex) noexcept {
  IRIS_LOG_ENTER();
  Expects(physicalDevice != VK_NULL_HANDLE);

  // Get all of the queue families again, so that we can get the number of
  // queues to create.

  std::uint32_t numQueueFamilyProperties;
  vkGetPhysicalDeviceQueueFamilyProperties2(physicalDevice,
                                            &numQueueFamilyProperties, nullptr);

  absl::FixedArray<VkQueueFamilyProperties2> queueFamilyProperties(
    numQueueFamilyProperties);
  for (auto& property : queueFamilyProperties) {
    property.sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2;
    property.pNext = nullptr;
  }

  vkGetPhysicalDeviceQueueFamilyProperties2(
    physicalDevice, &numQueueFamilyProperties, queueFamilyProperties.data());

  absl::FixedArray<float> priorities(
    queueFamilyProperties[queueFamilyIndex].queueFamilyProperties.queueCount);
  std::fill_n(std::begin(priorities), priorities.size(), 1.f);

  VkDeviceQueueCreateInfo qci = {};
  qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  qci.queueFamilyIndex = queueFamilyIndex;
  qci.queueCount =
    queueFamilyProperties[queueFamilyIndex].queueFamilyProperties.queueCount;
  qci.pQueuePriorities = priorities.data();

  VkDeviceCreateInfo ci = {};
  ci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  ci.pNext = &physicalDeviceFeatures;
  ci.queueCreateInfoCount = 1;
  ci.pQueueCreateInfos = &qci;
  ci.enabledExtensionCount =
    gsl::narrow_cast<std::uint32_t>(extensionNames.size());
  ci.ppEnabledExtensionNames = extensionNames.data();

  VkDevice device;
  if (auto result = vkCreateDevice(physicalDevice, &ci, nullptr, &device);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(
      std::system_error(make_error_code(result), "Cannot create device"));
  }

  Ensures(device != VK_NULL_HANDLE);
  IRIS_LOG_LEAVE();
  return std::make_pair(device, qci.queueCount);
} // iris::Renderer::CreateDevice

tl::expected<VmaAllocator, std::system_error>
iris::Renderer::CreateAllocator(VkPhysicalDevice physicalDevice,
                                VkDevice device) noexcept {
  IRIS_LOG_ENTER();
  Expects(physicalDevice != VK_NULL_HANDLE);
  Expects(device != VK_NULL_HANDLE);

  VmaAllocatorCreateInfo allocatorInfo = {};
  allocatorInfo.flags = VMA_ALLOCATOR_CREATE_KHR_DEDICATED_ALLOCATION_BIT;
  allocatorInfo.physicalDevice = physicalDevice;
  allocatorInfo.device = device;

  VmaAllocator allocator;
  if (auto result = vmaCreateAllocator(&allocatorInfo, &allocator);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(
      std::system_error(make_error_code(result), "Cannot create allocator"));
  }

  Ensures(allocator != VK_NULL_HANDLE);
  IRIS_LOG_LEAVE();
  return allocator;
} // iris::Renderer::CreateAllocator

tl::expected<absl::FixedArray<VkSurfaceFormatKHR>, std::system_error>
iris::Renderer::GetPhysicalDeviceSurfaceFormats(VkPhysicalDevice physicalDevice,
                                                VkSurfaceKHR surface) {
  std::uint32_t numSurfaceFormats;
  if (auto result = vkGetPhysicalDeviceSurfaceFormatsKHR(
        physicalDevice, surface, &numSurfaceFormats, nullptr);
      result != VK_SUCCESS) {
    return tl::unexpected(std::system_error(
      make_error_code(result), "Cannot get physical device surface formats"));
  }

  absl::FixedArray<VkSurfaceFormatKHR> surfaceFormats(numSurfaceFormats);
  if (auto result = vkGetPhysicalDeviceSurfaceFormatsKHR(
        physicalDevice, surface, &numSurfaceFormats, surfaceFormats.data());
      result != VK_SUCCESS) {
    return tl::unexpected(std::system_error(
      make_error_code(result), "Cannot get physical device surface formats"));
  }

  return surfaceFormats;
} // iris::Renderer::GetPhysicalDeviceSurfaceFormats

tl::expected<VkCommandBuffer, std::system_error>
iris::Renderer::BeginOneTimeSubmit(VkDevice device, VkCommandPool commandPool) noexcept {
  IRIS_LOG_ENTER();
  Expects(device != VK_NULL_HANDLE);
  Expects(commandPool != VK_NULL_HANDLE);

  VkCommandBufferAllocateInfo commandBufferAI = {};
  commandBufferAI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  commandBufferAI.commandPool = commandPool;
  commandBufferAI.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  commandBufferAI.commandBufferCount = 1;

  VkCommandBuffer commandBuffer;
  if (auto result =
        vkAllocateCommandBuffers(device, &commandBufferAI, &commandBuffer);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(make_error_code(result),
                                            "Cannot allocate command buffer"));
  }

  VkCommandBufferBeginInfo commandBufferBI = {};
  commandBufferBI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  commandBufferBI.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

  if (auto result = vkBeginCommandBuffer(commandBuffer, &commandBufferBI);
      result != VK_SUCCESS) {
    vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(make_error_code(result),
                                            "Cannot begin command buffer"));
  }

  IRIS_LOG_LEAVE();
  return commandBuffer;
} // iris::Renderer::BeginOneTimeSubmit

tl::expected<void, std::system_error>
iris::Renderer::EndOneTimeSubmit(VkCommandBuffer commandBuffer, VkDevice device,
                                 VkCommandPool commandPool, VkQueue queue,
                                 VkFence fence) noexcept {
  IRIS_LOG_ENTER();
  Expects(commandBuffer != VK_NULL_HANDLE);
  Expects(device != VK_NULL_HANDLE);
  Expects(commandPool != VK_NULL_HANDLE);
  Expects(queue != VK_NULL_HANDLE);
  Expects(fence != VK_NULL_HANDLE);

  VkSubmitInfo submitI = {};
  submitI.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitI.commandBufferCount = 1;
  submitI.pCommandBuffers = &commandBuffer;

  if (auto result = vkEndCommandBuffer(commandBuffer); result != VK_SUCCESS) {
    vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
    IRIS_LOG_LEAVE();
    return tl::unexpected(
      std::system_error(make_error_code(result), "Cannot end command buffer"));
  }

  if (auto result = vkQueueSubmit(queue, 1, &submitI, fence);
      result != VK_SUCCESS) {
    vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(make_error_code(result),
                                            "Cannot submit command buffer"));
  }

  if (auto result = vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);
      result != VK_SUCCESS) {
    vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(
      make_error_code(result), "Cannot wait on one-time submit fence"));
  }

  if (auto result = vkResetFences(device, 1, &fence); result != VK_SUCCESS) {
    vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(
      make_error_code(result), "Cannot reset one-time submit fence"));
  }

  vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
  IRIS_LOG_LEAVE();
  return {};
} // iris::Renderer::EndOneTimeSubmit

tl::expected<void, std::system_error> iris::Renderer::TransitionImage(
  VkDevice device, VkCommandPool commandPool, VkQueue queue, VkFence fence,
  VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout,
  std::uint32_t mipLevels, std::uint32_t arrayLayers) noexcept {
  IRIS_LOG_ENTER();
  Expects(image != VK_NULL_HANDLE);

  VkImageMemoryBarrier barrier = {};
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrier.oldLayout = oldLayout;
  barrier.newLayout = newLayout;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = image;
  barrier.subresourceRange.baseMipLevel = 0;
  barrier.subresourceRange.levelCount = mipLevels;
  barrier.subresourceRange.baseArrayLayer = 0;
  barrier.subresourceRange.layerCount = arrayLayers;

  if (newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    // FIXME: handle stencil
  } else {
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  }

  VkPipelineStageFlagBits srcStage;
  VkPipelineStageFlagBits dstStage;

  if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
      newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
  } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
             newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  } else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
             newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    dstStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
  } else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
             newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    dstStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  } else {
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(Error::kImageTransitionFailed,
                          "Not implemented"));
  }

  VkCommandBuffer commandBuffer;
  if (auto cb = BeginOneTimeSubmit(device, commandPool)) {
    commandBuffer = *cb;
  } else {
    IRIS_LOG_LEAVE();
    return tl::unexpected(cb.error());
  }

  vkCmdPipelineBarrier(commandBuffer, srcStage, dstStage, 0, 0, nullptr, 0,
                       nullptr, 1, &barrier);

  if (auto result =
        EndOneTimeSubmit(commandBuffer, device, commandPool, queue, fence);
      !result) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(result.error());
  }

  IRIS_LOG_LEAVE();
  return {};
} // iris::Renderer::TransitionImage

tl::expected<std::tuple<VkImage, VmaAllocation, VkImageView>, std::system_error>
iris::Renderer::AllocateImageAndView(
  VkDevice device, VmaAllocator allocator, VkFormat format, VkExtent2D extent,
  std::uint32_t mipLevels, std::uint32_t arrayLayers,
  VkSampleCountFlagBits sampleCount, VkImageUsageFlags imageUsage,
  VkImageTiling imageTiling, VmaMemoryUsage memoryUsage,
  VkImageSubresourceRange subresourceRange) noexcept {
  IRIS_LOG_ENTER();
  Expects(device != VK_NULL_HANDLE);
  Expects(allocator != VK_NULL_HANDLE);

  VkImageCreateInfo imageCI = {};
  imageCI.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageCI.imageType = VK_IMAGE_TYPE_2D;
  imageCI.format = format;
  imageCI.extent = {extent.width, extent.height, 1};
  imageCI.mipLevels = mipLevels;
  imageCI.arrayLayers = arrayLayers;
  imageCI.samples = sampleCount;
  imageCI.tiling = imageTiling;
  imageCI.usage = imageUsage;
  imageCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  imageCI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

  VmaAllocationCreateInfo allocationCI = {};
  allocationCI.usage = memoryUsage;

  VkImage image;
  VmaAllocation allocation;

  if (auto result = vmaCreateImage(allocator, &imageCI, &allocationCI, &image,
                                   &allocation, nullptr);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    tl::unexpected(
      std::system_error(make_error_code(result), "Cannot create image"));
  }

  VkImageViewCreateInfo imageViewCI = {};
  imageViewCI.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  imageViewCI.image = image;
  imageViewCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
  imageViewCI.format = format;
  imageViewCI.components = {
    VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
    VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY};
  imageViewCI.subresourceRange = subresourceRange;

  VkImageView imageView;
  if (auto result =
        vkCreateImageView(device, &imageViewCI, nullptr, &imageView);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    tl::unexpected(
      std::system_error(make_error_code(result), "Cannot create image view"));
  }

  IRIS_LOG_LEAVE();
  return std::make_tuple(image, allocation, imageView);
} // iris::Renderer::AllocateImageAndView

tl::expected<std::tuple<VkImage, VmaAllocation>, std::system_error>
iris::Renderer::CreateImage(VkDevice device, VmaAllocator allocator,
                            VkFormat format, VkExtent2D extent,
                            VkImageUsageFlags imageUsage,
                            VmaMemoryUsage memoryUsage,
                            gsl::not_null<std::byte*> pixels[[maybe_unused]],
                            std::uint32_t bytesPerPixel) noexcept {
  IRIS_LOG_ENTER();
  Expects(device != VK_NULL_HANDLE);
  Expects(allocator != VK_NULL_HANDLE);

  VkDeviceSize imageSize[[maybe_unused]];

  switch (format) {
  case VK_FORMAT_R8G8B8A8_UNORM:
    Expects(bytesPerPixel == sizeof(char) * 4);
    imageSize = extent.width * extent.height * sizeof(char) * 4;
    break;

  case VK_FORMAT_R32_SFLOAT:
    Expects(bytesPerPixel == sizeof(float));
    imageSize = extent.width * extent.height * sizeof(float);
    break;

  default:
    IRIS_LOG_LEAVE();
    return tl::unexpected(
      std::system_error(std::make_error_code(std::errc::invalid_argument),
                        "Unsupported texture format"));
  }

#if 0
  Buffer stagingBuffer;
  if (auto sb = Buffer::Create(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                               VMA_MEMORY_USAGE_CPU_TO_GPU)) {
    stagingBuffer = std::move(*sb);
  } else {
    IRIS_LOG_LEAVE();
    return tl::unexpected(
      std::system_error(sb.error().code(), "Cannot create staging buffer"));
  }

  if (auto p = stagingBuffer.Map<unsigned char*>()) {
    std::memcpy(*p, pixels, imageSize);
  } else {
    using namespace std::string_literals;
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(
      p.error().code(), "Cannot map staging buffer: "s + p.error().what()));
  }

  stagingBuffer.Unmap();
#endif

  VkImageCreateInfo imageCI = {};
  imageCI.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageCI.imageType = VK_IMAGE_TYPE_2D;
  imageCI.format = format;
  imageCI.extent = {extent.width, extent.height, 1};
  imageCI.mipLevels = 1;
  imageCI.arrayLayers = 1;
  imageCI.samples = VK_SAMPLE_COUNT_1_BIT;
  imageCI.tiling = VK_IMAGE_TILING_OPTIMAL;
  imageCI.usage = imageUsage | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  imageCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  imageCI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

  VmaAllocationCreateInfo allocationCI = {};
  allocationCI.usage = memoryUsage;

  VkImage image;
  VmaAllocation allocation;

  if (auto result = vmaCreateImage(allocator, &imageCI, &allocationCI, &image,
                                   &allocation, nullptr);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    tl::unexpected(
      std::system_error(make_error_code(result), "Cannot create image"));
  }

#if 0
  if (auto error = image.Transition(VK_IMAGE_LAYOUT_UNDEFINED,
                                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, 1,
                                    commandPool);
      error.code()) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(error);
  }

  VkCommandBuffer commandBuffer;
  if (auto cb = BeginOneTimeSubmit(commandPool)) {
    commandBuffer = *cb;
  } else {
    IRIS_LOG_LEAVE();
    return tl::unexpected(cb.error());
  }

  VkBufferImageCopy region = {};
  region.bufferOffset = 0;
  region.bufferRowLength = 0;
  region.bufferImageHeight = 0;
  region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  region.imageOffset = {0, 0, 0};
  region.imageExtent = extent;

  vkCmdCopyBufferToImage(commandBuffer, stagingBuffer.handle, image.handle,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

  if (auto error = EndOneTimeSubmit(commandBuffer, commandPool);
      error.code()) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(error);
  }

  if (auto error =
        image.Transition(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         (memoryUsage == VMA_MEMORY_USAGE_GPU_ONLY
                            ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                            : VK_IMAGE_LAYOUT_GENERAL),
                         1, 1, commandPool);
      error.code()) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(error);
  }
#endif

  IRIS_LOG_LEAVE();
  return std::make_tuple(image, allocation);
} // iris::Renderer::CreateImage

namespace iris::Renderer {

const TBuiltInResource DefaultTBuiltInResource = {
  /* .MaxLights = */ 32,
  /* .MaxClipPlanes = */ 6,
  /* .MaxTextureUnits = */ 32,
  /* .MaxTextureCoords = */ 32,
  /* .MaxVertexAttribs = */ 64,
  /* .MaxVertexUniformComponents = */ 4096,
  /* .MaxVaryingFloats = */ 64,
  /* .MaxVertexTextureImageUnits = */ 32,
  /* .MaxCombinedTextureImageUnits = */ 80,
  /* .MaxTextureImageUnits = */ 32,
  /* .MaxFragmentUniformComponents = */ 4096,
  /* .MaxDrawBuffers = */ 32,
  /* .MaxVertexUniformVectors = */ 128,
  /* .MaxVaryingVectors = */ 8,
  /* .MaxFragmentUniformVectors = */ 16,
  /* .MaxVertexOutputVectors = */ 16,
  /* .MaxFragmentInputVectors = */ 15,
  /* .MinProgramTexelOffset = */ -8,
  /* .MaxProgramTexelOffset = */ 7,
  /* .MaxClipDistances = */ 8,
  /* .MaxComputeWorkGroupCountX = */ 65535,
  /* .MaxComputeWorkGroupCountY = */ 65535,
  /* .MaxComputeWorkGroupCountZ = */ 65535,
  /* .MaxComputeWorkGroupSizeX = */ 1024,
  /* .MaxComputeWorkGroupSizeY = */ 1024,
  /* .MaxComputeWorkGroupSizeZ = */ 64,
  /* .MaxComputeUniformComponents = */ 1024,
  /* .MaxComputeTextureImageUnits = */ 16,
  /* .MaxComputeImageUniforms = */ 8,
  /* .MaxComputeAtomicCounters = */ 8,
  /* .MaxComputeAtomicCounterBuffers = */ 1,
  /* .MaxVaryingComponents = */ 60,
  /* .MaxVertexOutputComponents = */ 64,
  /* .MaxGeometryInputComponents = */ 64,
  /* .MaxGeometryOutputComponents = */ 128,
  /* .MaxFragmentInputComponents = */ 128,
  /* .MaxImageUnits = */ 8,
  /* .MaxCombinedImageUnitsAndFragmentOutputs = */ 8,
  /* .MaxCombinedShaderOutputResources = */ 8,
  /* .MaxImageSamples = */ 0,
  /* .MaxVertexImageUniforms = */ 0,
  /* .MaxTessControlImageUniforms = */ 0,
  /* .MaxTessEvaluationImageUniforms = */ 0,
  /* .MaxGeometryImageUniforms = */ 0,
  /* .MaxFragmentImageUniforms = */ 8,
  /* .MaxCombinedImageUniforms = */ 8,
  /* .MaxGeometryTextureImageUnits = */ 16,
  /* .MaxGeometryOutputVertices = */ 256,
  /* .MaxGeometryTotalOutputComponents = */ 1024,
  /* .MaxGeometryUniformComponents = */ 1024,
  /* .MaxGeometryVaryingComponents = */ 64,
  /* .MaxTessControlInputComponents = */ 128,
  /* .MaxTessControlOutputComponents = */ 128,
  /* .MaxTessControlTextureImageUnits = */ 16,
  /* .MaxTessControlUniformComponents = */ 1024,
  /* .MaxTessControlTotalOutputComponents = */ 4096,
  /* .MaxTessEvaluationInputComponents = */ 128,
  /* .MaxTessEvaluationOutputComponents = */ 128,
  /* .MaxTessEvaluationTextureImageUnits = */ 16,
  /* .MaxTessEvaluationUniformComponents = */ 1024,
  /* .MaxTessPatchComponents = */ 120,
  /* .MaxPatchVertices = */ 32,
  /* .MaxTessGenLevel = */ 64,
  /* .MaxViewports = */ 16,
  /* .MaxVertexAtomicCounters = */ 0,
  /* .MaxTessControlAtomicCounters = */ 0,
  /* .MaxTessEvaluationAtomicCounters = */ 0,
  /* .MaxGeometryAtomicCounters = */ 0,
  /* .MaxFragmentAtomicCounters = */ 8,
  /* .MaxCombinedAtomicCounters = */ 8,
  /* .MaxAtomicCounterBindings = */ 1,
  /* .MaxVertexAtomicCounterBuffers = */ 0,
  /* .MaxTessControlAtomicCounterBuffers = */ 0,
  /* .MaxTessEvaluationAtomicCounterBuffers = */ 0,
  /* .MaxGeometryAtomicCounterBuffers = */ 0,
  /* .MaxFragmentAtomicCounterBuffers = */ 1,
  /* .MaxCombinedAtomicCounterBuffers = */ 1,
  /* .MaxAtomicCounterBufferSize = */ 16384,
  /* .MaxTransformFeedbackBuffers = */ 4,
  /* .MaxTransformFeedbackInterleavedComponents = */ 64,
  /* .MaxCullDistances = */ 8,
  /* .MaxCombinedClipAndCullDistances = */ 8,
  /* .MaxSamples = */ 4,
  /* .maxMeshOutputVerticesNV = */ 256,
  /* .maxMeshOutputPrimitivesNV = */ 512,
  /* .maxMeshWorkGroupSizeX_NV = */ 32,
  /* .maxMeshWorkGroupSizeY_NV = */ 1,
  /* .maxMeshWorkGroupSizeZ_NV = */ 1,
  /* .maxTaskWorkGroupSizeX_NV = */ 32,
  /* .maxTaskWorkGroupSizeY_NV = */ 1,
  /* .maxTaskWorkGroupSizeZ_NV = */ 1,
  /* .maxMeshViewCountNV = */ 4,

  /* .limits = */
  {
    /* .nonInductiveForLoops = */ true,
    /* .whileLoops = */ true,
    /* .doWhileLoops = */ true,
    /* .generalUniformIndexing = */ true,
    /* .generalAttributeMatrixVectorIndexing = */ true,
    /* .generalVaryingIndexing = */ true,
    /* .generalSamplerIndexing = */ true,
    /* .generalVariableIndexing = */ true,
    /* .generalConstantMatrixVectorIndexing = */ true,
  }};

class DirStackIncluder : public glslang::TShader::Includer {
public:
  DirStackIncluder() noexcept = default;

  virtual IncludeResult* includeLocal(char const* headerName,
                                      char const* includerName,
                                      std::size_t inclusionDepth) override {
    return readLocalPath(headerName, includerName, inclusionDepth);
  }

  virtual IncludeResult* includeSystem(char const* headerName,
                                       char const* includerName
                                       [[maybe_unused]],
                                       std::size_t inclusionDepth
                                       [[maybe_unused]]) override {
    return readSystemPath(headerName);
  }

  virtual void releaseInclude(IncludeResult* result) override {
    if (result) {
      delete[] static_cast<char*>(result->userData);
      delete result;
    }
  }

  virtual void pushExternalLocalDirectory(std::string const& dir) {
    dirStack_.push_back(dir);
    numExternalLocalDirs_ = dirStack_.size();
  }

private:
  std::vector<std::string> dirStack_{};
  int numExternalLocalDirs_{0};

  virtual IncludeResult* readLocalPath(std::string const& headerName,
                                       std::string const& includerName,
                                       int depth) {
    // Discard popped include directories, and
    // initialize when at parse-time first level.
    dirStack_.resize(depth + numExternalLocalDirs_);

    if (depth == 1) dirStack_.back() = getDirectory(includerName);

    // Find a directory that works, using a reverse search of the include stack.
    for (auto& dir : dirStack_) {
      std::string path = dir + "/"s + headerName;
      std::replace(path.begin(), path.end(), '\\', '/');
      std::ifstream ifs(path.c_str(),
                        std::ios_base::binary | std::ios_base::ate);
      if (ifs) {
        dirStack_.push_back(getDirectory(path));
        return newIncludeResult(path, ifs, ifs.tellg());
      }
    }

    return nullptr;
  }

  virtual IncludeResult* readSystemPath(char const*) const {
    GetLogger()->error("including system headers not implemented");
    return nullptr;
  }

  virtual IncludeResult* newIncludeResult(std::string const& path,
                                          std::ifstream& ifs,
                                          int length) const {
    char* content = new char[length];
    ifs.seekg(0, ifs.beg);
    ifs.read(content, length);
    return new IncludeResult(path.c_str(), content, length, content);
  }

  // If no path markers, return current working directory.
  // Otherwise, strip file name and return path leading up to it.
  virtual std::string getDirectory(const std::string path) const {
    size_t last = path.find_last_of("/\\");
    return last == std::string::npos ? "." : path.substr(0, last);
  }
}; // class DirStackIncluder

[[nodiscard]] static tl::expected<std::vector<std::uint32_t>, std::string>
CompileShader(std::string_view source, VkShaderStageFlagBits shaderStage,
              filesystem::path const& path,
              gsl::span<std::string> macroDefinitions [[maybe_unused]],
              std::string const& entryPoint) {
  IRIS_LOG_ENTER();
  Expects(source.size() > 0);

  auto const lang = [&shaderStage]() {
    if ((shaderStage & VK_SHADER_STAGE_VERTEX_BIT)) {
      return EShLanguage::EShLangVertex;
    } else if ((shaderStage & VK_SHADER_STAGE_FRAGMENT_BIT)) {
      return EShLanguage::EShLangFragment;
    } else {
      GetLogger()->critical("Unhandled shaderStage: {}", shaderStage);
      std::terminate();
    }
  }();

  char const* strings[] = {source.data()};
  int lengths[] = {static_cast<int>(source.size())};
  char const* names[] = {path.string().c_str()};

  glslang::TShader shader(lang);
  shader.setStringsWithLengthsAndNames(strings, lengths, names, 1);
  shader.setEntryPoint(entryPoint.c_str());
  shader.setEnvInput(glslang::EShSource::EShSourceGlsl, lang,
                     glslang::EShClient::EShClientVulkan, 101);
  shader.setEnvClient(glslang::EShClient::EShClientVulkan,
                      glslang::EShTargetClientVersion::EShTargetVulkan_1_1);
  shader.setEnvTarget(glslang::EShTargetLanguage::EShTargetSpv,
                      glslang::EShTargetLanguageVersion::EShTargetSpv_1_0);

  DirStackIncluder includer;
  includer.pushExternalLocalDirectory(kIRISContentDirectory);

  if (!shader.parse(&DefaultTBuiltInResource, 1, false,
                    EShMessages::EShMsgDefault, includer)) {
    return tl::unexpected(std::string(shader.getInfoLog()));
  }

  glslang::TProgram program;
  program.addShader(&shader);

  if (!program.link(EShMessages::EShMsgDefault)) {
    return tl::unexpected(std::string(program.getInfoLog()));
  }

  if (auto glsl = program.getIntermediate(lang)) {
    glslang::SpvOptions options;
    options.validate = true;
#ifndef NDEBUG
    options.generateDebugInfo = true;
#endif

    spv::SpvBuildLogger logger;
    std::vector<std::uint32_t> code;
    glslang::GlslangToSpv(*glsl, code, &logger, &options);

    Ensures(code.size() > 0);
    IRIS_LOG_LEAVE();
    return code;
  } else {
    return tl::unexpected(std::string(
      "cannot get glsl intermediate representation of compiled shader"));
  }
} // CompileShader

} // namespace iris::Renderer

tl::expected<VkShaderModule, std::system_error>
iris::Renderer::CompileShaderFromSource(VkDevice device,
                                        std::string_view source,
                                        VkShaderStageFlagBits stage,
                                        std::string name) noexcept {
  IRIS_LOG_ENTER();
  Expects(device != VK_NULL_HANDLE);
  Expects(source.size() > 0);

  VkShaderModule module{VK_NULL_HANDLE};

  auto code = CompileShader(source, stage, "<inline>", {}, "main");
  if (!code) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(
      std::system_error(Error::kShaderCompileFailed, code.error()));
  }

  VkShaderModuleCreateInfo shaderModuleCI = {};
  shaderModuleCI.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  // codeSize is count of bytes, not count of words (which is what size() is)
  shaderModuleCI.codeSize = gsl::narrow_cast<std::uint32_t>(code->size()) * 4u;
  shaderModuleCI.pCode = code->data();

  if (auto result =
        vkCreateShaderModule(device, &shaderModuleCI, nullptr, &module);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(make_error_code(result),
                                            "Cannot create shader module"));
  }

  if (!name.empty()) {
    NameObject(device, VK_OBJECT_TYPE_SHADER_MODULE, module, name.c_str());
  }

  Ensures(module != VK_NULL_HANDLE);
  IRIS_LOG_LEAVE();
  return module;
} // iris::Renderer::CompileShaderFromSource
