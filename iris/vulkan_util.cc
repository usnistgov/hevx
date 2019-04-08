#include "vulkan_util.h"
#include "absl/container/fixed_array.h"
#include "enumerate.h"
#include "error.h"
#include "io/read_file.h"
#include "logging.h"
#include <string>

tl::expected<VkInstance, std::system_error> iris::vk::CreateInstance(
  gsl::czstring<> appName, std::uint32_t appVersion,
  gsl::span<gsl::czstring<>> extensionNames,
  gsl::span<gsl::czstring<>> layerNames,
  PFN_vkDebugUtilsMessengerCallbackEXT debugUtilsMessengerCallback) noexcept {
  IRIS_LOG_ENTER();

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
} // iris::vk::CreateInstance

tl::expected<VkDebugUtilsMessengerEXT, std::system_error>
iris::vk::CreateDebugUtilsMessenger(
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
} // iris::vk::CreateDebugUtilsMessenger

bool iris::vk::ComparePhysicalDeviceFeatures(
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
} // iris::vk::ComparePhysicalDeviceFeatures

tl::expected<std::uint32_t, std::system_error>
iris::vk::GetQueueFamilyIndex(VkPhysicalDevice physicalDevice,
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
      return gsl::narrow_cast<std::uint32_t>(i);
    }
  }

  IRIS_LOG_LEAVE();
  return UINT32_MAX;
} // iris::vk::GetQueueFamilyIndex

tl::expected<bool, std::system_error> iris::vk::IsPhysicalDeviceGood(
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
} // iris::vk::IsPhysicalDeviceGood

tl::expected<void, std::system_error>
iris::vk::DumpPhysicalDevice(VkPhysicalDevice physicalDevice,
                             char const* indent) noexcept {
  IRIS_LOG_ENTER();
  Expects(physicalDevice != VK_NULL_HANDLE);

  //
  // Get the properties.
  //

  VkPhysicalDeviceRayTracingPropertiesNV rayTracingProps = {};
  rayTracingProps.sType =
    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PROPERTIES_NV;

  VkPhysicalDeviceMultiviewProperties multiviewProps = {};
  multiviewProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_PROPERTIES;
  multiviewProps.pNext = &rayTracingProps;

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

  GetLogger()->debug("{}{} Extensions:", indent, numExtensionProperties);
  for (auto&& extensionProperty : extensionProperties) {
    GetLogger()->debug("{}  {}", indent, extensionProperty.extensionName);
  }

  IRIS_LOG_LEAVE();
  return {};
} // iris::vk::DumpPhysicalDevice

tl::expected<void, std::system_error>
iris::vk::DumpPhysicalDevices(VkInstance instance) noexcept {
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
  for (auto&& [i, physicalDevice] : enumerate(physicalDevices)) {
    GetLogger()->debug("Physical device {}:", i);
    DumpPhysicalDevice(physicalDevice, "  ");
  }

  IRIS_LOG_LEAVE();
  return {};
} // iris::vk::DumpPhysicalDevices

tl::expected<VkPhysicalDevice, std::system_error>
iris::vk::ChoosePhysicalDevice(VkInstance instance,
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
} // iris::vk::ChoosePhysicalDevice

tl::expected<std::tuple<VkDevice, std::uint32_t>, std::system_error>
iris::vk::CreateDevice(VkPhysicalDevice physicalDevice,
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

  absl::InlinedVector<VkDeviceQueueCreateInfo, 1> queueCreateInfos;
  queueCreateInfos.push_back(
    {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, nullptr, 0, queueFamilyIndex,
     queueFamilyProperties[queueFamilyIndex].queueFamilyProperties.queueCount,
     priorities.data()});

  VkDeviceCreateInfo ci = {};
  ci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  ci.pNext = &physicalDeviceFeatures;
  ci.queueCreateInfoCount =
    gsl::narrow_cast<std::uint32_t>(queueCreateInfos.size());
  ci.pQueueCreateInfos = queueCreateInfos.data();
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
  return std::make_tuple(device, queueCreateInfos[0].queueCount);
} // iris::vk::CreateDevice

tl::expected<VmaAllocator, std::system_error>
iris::vk::CreateAllocator(VkPhysicalDevice physicalDevice,
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
} // iris::vk::CreateAllocator

tl::expected<absl::InlinedVector<VkSurfaceFormatKHR, 128>, std::system_error>
iris::vk::GetPhysicalDeviceSurfaceFormats(VkPhysicalDevice physicalDevice,
                                          VkSurfaceKHR surface) {
  std::uint32_t numSurfaceFormats;
  if (auto result = vkGetPhysicalDeviceSurfaceFormatsKHR(
        physicalDevice, surface, &numSurfaceFormats, nullptr);
      result != VK_SUCCESS) {
    return tl::unexpected(std::system_error(
      make_error_code(result), "Cannot get physical device surface formats"));
  }

  absl::InlinedVector<VkSurfaceFormatKHR, 128> surfaceFormats(
    numSurfaceFormats);
  if (auto result = vkGetPhysicalDeviceSurfaceFormatsKHR(
        physicalDevice, surface, &numSurfaceFormats, surfaceFormats.data());
      result != VK_SUCCESS) {
    return tl::unexpected(std::system_error(
      make_error_code(result), "Cannot get physical device surface formats"));
  }

  return surfaceFormats;
} // iris::vk::GetPhysicalDeviceSurfaceFormats

void iris::vk::SetImageLayout(VkCommandBuffer commandBuffer, VkImage image,
                              VkPipelineStageFlags srcStages,
                              VkPipelineStageFlags dstStages,
                              VkImageLayout oldLayout, VkImageLayout newLayout,
                              VkImageAspectFlags aspectMask,
                              std::uint32_t mipLevels,
                              std::uint32_t arrayLayers) noexcept {
  VkImageMemoryBarrier barrier = {};
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrier.oldLayout = oldLayout;
  barrier.newLayout = newLayout;
  barrier.image = image;
  barrier.subresourceRange.aspectMask = aspectMask;
  barrier.subresourceRange.baseMipLevel = 0;
  barrier.subresourceRange.levelCount = mipLevels;
  barrier.subresourceRange.baseArrayLayer = 0;
  barrier.subresourceRange.layerCount = arrayLayers;

  switch (oldLayout) {
  case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
    barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    break;
  case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    break;
  case VK_IMAGE_LAYOUT_PREINITIALIZED:
    barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
    break;
  default: break;
  }

  switch (newLayout) {
  case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    break;

  case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    break;

  case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    break;

  case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
    barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    break;

  case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
    barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    break;

  default: break;
  }

  vkCmdPipelineBarrier(commandBuffer, // commandBuffer
                       srcStages,     // srcStageMask
                       dstStages,     // dstStageMask
                       0,             // dependencyFlags
                       0,             // memoryBarrierCount
                       nullptr,       // pMemoryBarriers
                       0,             // bufferMemoryBarrierCount
                       nullptr,       // pBufferMemoryBarriers
                       1,             // imageMemoryBarrierCount
                       &barrier       // pImageMemoryBarriers
  );
} // iris::vk::SetImageLayout
