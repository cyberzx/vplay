#include  "vrenderer.h"
#include  "vulkantools.h"

#include  <algorithm>
#include  <string>
#include  <stdexcept>
#include  <stdio.h>
#include  <string.h>


void  VKRenderer::getMemoryType(uint32_t type_bits, VkFlags requirements_mask, uint32_t* type_index)
{
  if (!activeGPU)
    throw vulkan_error("failed to get memory type: GPU not selected");

  GPUInfo const& gpuInfo = systemGPUs[activeGPU];
  for (uint32_t i = 0; i < gpuInfo.memoryProps.memoryTypeCount; ++i)
  {
    if ((type_bits & 1) == 1)
    {
      if ((gpuInfo.memoryProps.memoryTypes[i].propertyFlags & requirements_mask) ==
          requirements_mask)
      {
        *type_index = i;
        return;
      }
    }
    type_bits >> 1;
  }

  throw vulkan_error("failed to find requirenment memory properties");
}

void  VKRenderer::initInstance(const char* app_name, const char* engine_name)
{
  if (!vktools::get_vk_array(vkEnumerateInstanceLayerProperties, layers))
    throw vulkan_error("failed to get layers");

  if (!vktools::get_vk_array(vkEnumerateInstanceExtensionProperties, extensions, nullptr))
    throw vulkan_error("failed to get extension properties");

  for (VkLayerProperties const& props: layers)
  {
    std::vector<VkExtensionProperties>  layerExtensions;
    if (vktools::get_vk_array(vkEnumerateInstanceExtensionProperties, layerExtensions,
                              props.layerName))
    {

      if (!layerExtensions.empty())
        layersExtensions[props.layerName] = std::move(layerExtensions);
    }
  }

  std::vector<const char*>  usedInstanceExtensions = chooseExtensions();
  std::vector<const char*>  usedLayers = chooseLayers();

  if (!usedInstanceExtensions.empty())
    printf("[VULKAN] used extensions: ");
  for (const char* ext: usedInstanceExtensions)
    if (ext != usedInstanceExtensions.back())
      printf("%s, ", ext);
    else
      printf("%s\n", ext);

  if (!usedLayers.empty())
    printf("[VULKAN] used layers: ");
  for (const char* lay: usedLayers)
    if (lay != usedLayers.back())
      printf("%s, ", lay);
    else
      printf("%s\n", lay);

  const VkApplicationInfo app = {
    .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
    .pNext = nullptr,
    .pApplicationName = app_name,
    .applicationVersion = 0,
    .pEngineName = engine_name,
    .engineVersion = 0,
    .apiVersion = VK_API_VERSION_1_0,
  };

  VkInstanceCreateInfo instInfo = {
    .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
    .pNext = nullptr,
    .flags = 0,
    .pApplicationInfo = &app,
    .enabledLayerCount = (uint32_t)usedLayers.size(),
    .ppEnabledLayerNames = usedLayers.empty() ? nullptr : usedLayers.data(),
    .enabledExtensionCount = (uint32_t)usedInstanceExtensions.size(),
    .ppEnabledExtensionNames = usedInstanceExtensions.empty() ? nullptr :
                               usedInstanceExtensions.data(),
  };

  vktools::checked_call(vkCreateInstance, "failed to create vulkan instance",
                        &instInfo, nullptr, &instance);

  collectGPUsInfo();
}

void  VKRenderer::chooseGPU(VkSurfaceKHR window_surface)
{
  windowSurface = window_surface;

  for (std::pair<VkPhysicalDevice, const GPUInfo> const& gpu: systemGPUs)
  {
    const GPUInfo& gpuInfo = gpu.second;
    bool  hasSuitableQueue = false;

    for (size_t i = 0; i < gpuInfo.queueFamilies.size(); ++i)
    {
      VkQueueFamilyProperties const& queueFamily = gpuInfo.queueFamilies[i];
      unsigned int requiredQueueFlags = VK_QUEUE_GRAPHICS_BIT & VK_QUEUE_TRANSFER_BIT;
      if ((queueFamily.queueFlags & requiredQueueFlags) == requiredQueueFlags)
      {
        VkBool32 presentSupport = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(gpu.first, i, window_surface, &presentSupport);
        if (presentSupport)
        {
          hasSuitableQueue = true;
          graphicsQueueFamilyIdx = i;
          break;
        }
      }
    }

    if (hasSuitableQueue)
    {
      activeGPU = gpu.first;
      printf("[VULKAN] Found suitable GPU: %s\n", gpuInfo.props.deviceName);
      break;
    }
  }

  if (activeGPU == VK_NULL_HANDLE)
    throw vulkan_error("failed to find suitable GPU");

  float queuePriorities = 1.0f;
  VkDeviceQueueCreateInfo queueCreateInfo = {
    .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
    .pNext = nullptr,
    .flags = 0,
    .queueFamilyIndex = graphicsQueueFamilyIdx,
    .queueCount = 1,
    .pQueuePriorities = &queuePriorities
  };

  auto deviceExtensions = chooseDeviceExtensions(systemGPUs[activeGPU]);

  VkDeviceCreateInfo deviceCreateInfo = {
    .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
    .pNext = nullptr,
    .flags = 0,
    .queueCreateInfoCount = 1,
    .pQueueCreateInfos = &queueCreateInfo,
    .enabledLayerCount = 0,
    .ppEnabledLayerNames = nullptr,
    .enabledExtensionCount = (uint32_t)deviceExtensions.size(),
    .ppEnabledExtensionNames = deviceExtensions.empty() ? nullptr : deviceExtensions.data(),
    .pEnabledFeatures = nullptr
  };

  vktools::checked_call(vkCreateDevice, "failed to create logical device",
                        activeGPU, &deviceCreateInfo, nullptr, &device);


  VkCommandPoolCreateInfo commandPoolInfo = {
    .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
    .pNext = nullptr,
    .flags = 0,
    .queueFamilyIndex = graphicsQueueFamilyIdx
  };

  vktools::checked_call(vkCreateCommandPool, "failed to create command pool",
                        device, &commandPoolInfo, nullptr, &commandPool);

  commandBuffers.resize(1);
  VkCommandBufferAllocateInfo commandBufferAllocInfo = {
    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
    .pNext = nullptr,
    .commandPool = commandPool,
    .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
    .commandBufferCount = (uint32_t)commandBuffers.size()
  };

  vktools::checked_call(vkAllocateCommandBuffers, "failed to allocate command buffers",
                        device, &commandBufferAllocInfo, commandBuffers.data());

}

void  VKRenderer::onResize(VkSurfaceKHR window_surface)
{
  windowSurface = window_surface;
  createSwapchain();
}

void  VKRenderer::createSwapchain()
{
  VkSurfaceCapabilitiesKHR surfCaps;
  vktools::checked_call(vkGetPhysicalDeviceSurfaceCapabilitiesKHR, "failed to get surface caps",
                        activeGPU, windowSurface, &surfCaps);

  printf("\nsurface capabilities:\n");
  printf("\tminImageCount %d, maxImageCount %d\n", surfCaps.minImageCount, surfCaps.maxImageCount);
  printf("\tcurrentExtent %dx%d\n", surfCaps.currentExtent.width, surfCaps.currentExtent.height);
  printf("\tminImageExtent %dx%d\n", surfCaps.minImageExtent.width, surfCaps.minImageExtent.height);
  printf("\tmaxImageExtent %dx%d\n", surfCaps.maxImageExtent.width, surfCaps.maxImageExtent.height);
  printf("\tmaxImageArrayLayers %d\n", surfCaps.maxImageArrayLayers);

  std::vector<VkSurfaceFormatKHR> surfFormats;
  if (!vktools::get_vk_array(vkGetPhysicalDeviceSurfaceFormatsKHR, surfFormats,
                             activeGPU, windowSurface))
    throw std::runtime_error("failed to get surface formats");

  int surfFormatIdx = -1;
  for (size_t i = 0; i < surfFormats.size(); ++i)
  {
    if (surfFormats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR &&
        surfFormats[i].format     == VK_FORMAT_B8G8R8A8_UNORM)
    {
      surfFormatIdx = i;
      break;
    }
  }

  std::vector<VkPresentModeKHR> presentModes;
  if (!vktools::get_vk_array(vkGetPhysicalDeviceSurfacePresentModesKHR, presentModes,
                             activeGPU, windowSurface))
    throw std::runtime_error("failed to get present modes");

  VkPresentModeKHR desiredPM = VK_PRESENT_MODE_MAILBOX_KHR;
  if (std::find(presentModes.begin(), presentModes.end(), VK_PRESENT_MODE_MAILBOX_KHR) ==
      presentModes.end())
  {
    desiredPM = presentModes.front();
  }

  VkSwapchainCreateInfoKHR swapchainCreateInfo = {
    .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
    .pNext = nullptr,
    .flags = 0,
    .surface = windowSurface,
    .minImageCount = 3,
    .imageFormat = surfFormats[surfFormatIdx].format,
    .imageColorSpace = surfFormats[surfFormatIdx].colorSpace,
    .imageExtent = surfCaps.currentExtent,
    .imageArrayLayers = 1,
    .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
    .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
    .queueFamilyIndexCount = 0,
    .pQueueFamilyIndices = nullptr,
    .preTransform = surfCaps.currentTransform,
    .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
    .presentMode = desiredPM,
    .clipped = VK_TRUE,
    .oldSwapchain = swapchain
  };

  vktools::checked_call(vkCreateSwapchainKHR, "failed to create swapchain",
                        device, &swapchainCreateInfo,
                        nullptr, &swapchain);

  if (!vktools::get_vk_array(vkGetSwapchainImagesKHR, swapchainImages, device, swapchain))
    throw std::runtime_error("failed to retrieve swapchain images");

  swapchainFormat = swapchainCreateInfo.imageFormat;
  swapchainExtent = swapchainCreateInfo.imageExtent;

  for (VkImage image: swapchainImages)
  {
    VkImageViewCreateInfo imageViewCreateInfo = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      .pNext = nullptr,
      .flags = 0,
      .image = image,
      .viewType = VK_IMAGE_VIEW_TYPE_2D,
      .format = swapchainFormat,
      .components = {
        .r = VK_COMPONENT_SWIZZLE_IDENTITY,
        .g = VK_COMPONENT_SWIZZLE_IDENTITY,
        .b = VK_COMPONENT_SWIZZLE_IDENTITY,
        .a = VK_COMPONENT_SWIZZLE_IDENTITY,
      },
      .subresourceRange = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = 0,
        .levelCount = 1,
        .baseArrayLayer = 0,
        .layerCount = 1
      }
    };

    VkImageView  imageView;
    vktools::checked_call(vkCreateImageView, "failed to create image view",
                          device, &imageViewCreateInfo, nullptr, &imageView);
    swapchainViews.push_back(imageView);
  }

  createDepthbuffer();
}

void  VKRenderer::createDepthbuffer()
{
  VkImageCreateInfo imageCreateInfo = {
    .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
    .pNext = NULL,
    .flags = 0,
    .imageType = VK_IMAGE_TYPE_2D,
    .format = VK_FORMAT_D16_UNORM,
    .extent = {
      .width = swapchainExtent.width,
      .height= swapchainExtent.height,
      .depth = 1
    },
    .mipLevels = 1,
    .arrayLayers = 1,
    .samples = VK_SAMPLE_COUNT_1_BIT,
    .tiling  = VK_IMAGE_TILING_OPTIMAL,
    .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
    .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    .queueFamilyIndexCount = 0,
    .pQueueFamilyIndices = NULL,
    .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
  };

  vktools::checked_call(vkCreateImage, "failed to create depth buffer image",
                        device, &imageCreateInfo, nullptr, &depthBufferImage);

  VkMemoryRequirements memReqs;
  vkGetImageMemoryRequirements(device, depthBufferImage, &memReqs);

  VkMemoryAllocateInfo  memAllocInfo = {
    .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
    .pNext = nullptr,
    .allocationSize = memReqs.size
  };

  getMemoryType(memReqs.memoryTypeBits, 0, &memAllocInfo.memoryTypeIndex);
  vktools::checked_call(vkAllocateMemory, "failed to allocate depth buffer memory",
                       device, &memAllocInfo, nullptr, &depthBufferMemory);

  vktools::checked_call(vkBindImageMemory, "failed to bind memory to depth buffer",
                       device, depthBufferImage, depthBufferMemory, 0);

  VkImageViewCreateInfo imageViewCreateInfo = {
    .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
    .pNext = nullptr,
    .flags = 0,
    .image = depthBufferImage,
    .viewType = VK_IMAGE_VIEW_TYPE_2D,
    .format = VK_FORMAT_D16_UNORM,
    .components = {
      .r = VK_COMPONENT_SWIZZLE_IDENTITY,
      .g = VK_COMPONENT_SWIZZLE_IDENTITY,
      .b = VK_COMPONENT_SWIZZLE_IDENTITY,
      .a = VK_COMPONENT_SWIZZLE_IDENTITY,
    },
    .subresourceRange = {
      .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
      .baseMipLevel = 0,
      .levelCount = 1,
      .baseArrayLayer = 0,
      .layerCount = 1
    }
  };

  vktools::checked_call(vkCreateImageView, "failed to create depth buffer image view",
                        device, &imageViewCreateInfo, nullptr, &depthBufferView);
}

void  VKRenderer::collectGPUsInfo()
{
  std::vector<VkPhysicalDevice>       physicalDevices;
  if (!vktools::get_vk_array(vkEnumeratePhysicalDevices, physicalDevices, instance))
    throw vulkan_error("failed to get physical GPUs");

  for (VkPhysicalDevice dev: physicalDevices)
  {
    GPUInfo   gpuInfo;
    vkGetPhysicalDeviceProperties(dev, &gpuInfo.props);
    vkGetPhysicalDeviceMemoryProperties(dev, &gpuInfo.memoryProps);
    vkGetPhysicalDeviceFeatures(dev, &gpuInfo.features);
    if (!vktools::get_vk_array(vkEnumerateDeviceExtensionProperties, gpuInfo.extensions, dev, nullptr))
      throw vulkan_error("failed to get device extensions properties");
    if (!vktools::get_vk_array(vkGetPhysicalDeviceQueueFamilyProperties, gpuInfo.queueFamilies, dev))
      throw vulkan_error("failed to get device queue families");
    systemGPUs.emplace(dev, std::move(gpuInfo));
  }
}

bool  VKRenderer::hasExtension(const char* ext_name,
                    std::vector<VkExtensionProperties> const& extensions) const
{
  for (VkExtensionProperties const& props: extensions)
  {
    if (!strcmp(props.extensionName, ext_name))
      return true;
  }
  return false;
}

bool  VKRenderer::hasLayer(const char* layer_name) const
{
  for (VkLayerProperties const& props: layers)
  {
    if (!strcmp(props.layerName, layer_name))
      return true;
  }
  return false;
}

std::vector<const char*>  VKRenderer::chooseDeviceExtensions(GPUInfo& gpu)
{
  const char* optional[] = {};
  const char* required[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_NV_GLSL_SHADER_EXTENSION_NAME};
  std::vector<const char*>  result;

  for (const char* ext_name: optional)
  {
    if (hasExtension(ext_name, gpu.extensions))
      result.push_back(ext_name);
    else
      printf("[VULKAN] Optional device extension %s is not supported\n", ext_name);
  }

  for (const char* ext_name: required)
  {
    if (hasExtension(ext_name, gpu.extensions))
      result.push_back(ext_name);
    else
    {
      std::string message = "Required extension ";
      message += ext_name;
      message += " is not supported";
      throw vulkan_error(message);
    }
  }

  return result;
}

std::vector<const char*>  VKRenderer::chooseExtensions()
{
  const char* optional[] = {VK_EXT_DEBUG_REPORT_EXTENSION_NAME};
  const char* required[] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_XCB_SURFACE_EXTENSION_NAME};

  std::vector<const char*>  result;
  for (const char* ext_name: optional)
  {
    if (hasExtension(ext_name, extensions))
      result.push_back(ext_name);
    else
      printf("[VULKAN] Optional instance extension %s is not supported\n", ext_name);
  }

  for (const char* ext_name: required)
  {
    if (hasExtension(ext_name, extensions))
      result.push_back(ext_name);
    else
    {
      std::string message = "Required extension ";
      message += ext_name;
      message += " is not supported";
      throw vulkan_error(message);
    }
  }

  return result;
}

std::vector<const char*>  VKRenderer::chooseLayers()
{
  const char* optional[] = {"VK_LAYER_LUNARG_core_validation",
                            "VK_LAYER_LUNARG_standard_validation",
                            "VK_LAYER_LUNARG_parameter_validation",
                            "VK_LAYER_LUNARG_object_tracker",
                            "VK_LAYER_LUNARG_swapchain",
                            };


  std::vector<const char*>  result;
  for (const char* layer: optional)
  {
    if (hasLayer(layer))
      result.push_back(layer);
    else
      printf("[VULKAN] Optional layer %s is not supported\n", layer);
  }

  return result;
}

