#include  "vrenderer.h"
#include  "vulkantools.h"

#include  <string>
#include  <stdexcept>
#include  <stdio.h>
#include  <string.h>

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

