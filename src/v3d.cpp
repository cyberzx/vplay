#include "v3d.h"
#include "vulkantools.h"

#include  <vector>
#include  <unordered_map>
#include  <stdio.h>

namespace v3d {

struct GPUInfo
{
  vk::PhysicalDevice                      device;
  vk::PhysicalDeviceProperties            props;
  vk::PhysicalDeviceMemoryProperties      memoryProps;
  vk::PhysicalDeviceFeatures              features;
  std::vector<vk::ExtensionProperties>    extensions;
  std::vector<vk::QueueFamilyProperties>  queueFamilies;

  int   renderQueueFamilyIdx = -1;
};

static std::vector<vk::LayerProperties>      layers;
static std::vector<vk::ExtensionProperties>  extensions;
static std::unordered_map<std::string, decltype(extensions)>  layers_extensions;

static vk::Instance   instance;
static vk::Device     device;
static vk::SwapchainKHR   swapchain;
static std::vector<vk::ImageView> swapchain_views;

static struct 
{
  vk::Image          image;
  vk::ImageView      view;
  vk::DeviceMemory   memory;
} depth_buffer;


static std::vector<GPUInfo> system_GPUs;
static int active_GPU = -1;

static VkSurfaceKHR   window_surface;

static int  present_mode_value(vk::PresentModeKHR pm)
{
  switch (pm)
  {
    case vk::PresentModeKHR::eMailbox: return 5;
    case vk::PresentModeKHR::eFifo: return 4;
    case vk::PresentModeKHR::eFifoRelaxed: return 3;
    case vk::PresentModeKHR::eImmediate: return 2;
    default: return 0;
  }
}

static void enum_layers_and_extensions()
{
  layers = vk::enumerateInstanceLayerProperties();
  extensions = vk::enumerateInstanceExtensionProperties();
 
  for (vk::LayerProperties const& props: layers)
  {
    auto layerExtensions = vk::enumerateInstanceExtensionProperties(
                                std::string(props.layerName));
    if (!layerExtensions.empty())
      layers_extensions[props.layerName] = std::move(layerExtensions);
  }
}

static bool  layer_supported(const char* layer_name)
{
  for (VkLayerProperties const& props: layers)
  {
    if (!strcmp(props.layerName, layer_name))
      return true;
  }
  return false;
}

static bool  extension_suported(const char* ext_name,
                         std::vector<vk::ExtensionProperties> const& extensions)
{
  for (vk::ExtensionProperties const& props: extensions)
  {
    if (!strcmp(props.extensionName, ext_name))
      return true;
  }
  return false;
}

static std::vector<const char*>  choose_extensions()
{
  const char* optional[] = {VK_EXT_DEBUG_REPORT_EXTENSION_NAME};
  const char* required[] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_XCB_SURFACE_EXTENSION_NAME};

  std::vector<const char*>  result;
  for (const char* ext_name: optional)
  {
    if (extension_suported(ext_name, extensions))
      result.push_back(ext_name);
    else
      printf("[VULKAN] Optional instance extension %s is not supported\n", ext_name);
  }

  for (const char* ext_name: required)
  {
    if (extension_suported(ext_name, extensions))
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

static std::vector<const char*> choose_device_extensions(GPUInfo const& gpu)
{
  const char* optional[] = {};
  const char* required[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_NV_GLSL_SHADER_EXTENSION_NAME};
  std::vector<const char*>  result;

  for (const char* ext_name: optional)
  {
    if (extension_suported(ext_name, gpu.extensions))
      result.push_back(ext_name);
    else
      printf("[VULKAN] Optional device extension %s is not supported\n", ext_name);
  }

  for (const char* ext_name: required)
  {
    if (extension_suported(ext_name, gpu.extensions))
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

static std::vector<const char*>  choose_layers()
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
    if (layer_supported(layer))
      result.push_back(layer);
    else
      printf("[VULKAN] Optional layer %s is not supported\n", layer);
  }

  return result;
}

static void enum_GPUs()
{
  std::vector<vk::PhysicalDevice> physicalDevices = instance.enumeratePhysicalDevices();
  for (vk::PhysicalDevice const& dev: physicalDevices)
  {
    GPUInfo   gpuInfo = {
      .device = dev,
      .props = dev.getProperties(),
      .memoryProps = dev.getMemoryProperties(),
      .features = dev.getFeatures(),
      .extensions = dev.enumerateDeviceExtensionProperties(),
      .queueFamilies = dev.getQueueFamilyProperties() 
    };
    system_GPUs.push_back(std::move(gpuInfo));
  }
}


static uint32_t find_memory_type(uint32_t type_bits, 
                                 vk::MemoryPropertyFlags requirements_mask)
{
  GPUInfo const& gpuInfo = system_GPUs[active_GPU];
  for (uint32_t i = 0; i < gpuInfo.memoryProps.memoryTypeCount; ++i)
  {
    vk::MemoryPropertyFlags memoryFlags = gpuInfo.memoryProps.memoryTypes[i].propertyFlags;
    if ((memoryFlags & requirements_mask) != requirements_mask)
      continue;
    if (type_bits & (1<<i))
      return i;
  }

  throw vulkan_error("failed to find required memory properties");
}

void  init(const char* app_name, const char* engine_name)
{
  enum_layers_and_extensions();

  std::vector<const char*>  usedInstanceExtensions = choose_extensions();
  std::vector<const char*>  usedLayers = choose_layers();

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

  auto const app = vk::ApplicationInfo()
                        .setPApplicationName(app_name)
                        .setApplicationVersion(0)
                        .setPEngineName(engine_name)
                        .setEngineVersion(0)
                        .setApiVersion(VK_API_VERSION_1_0);

  instance = vk::createInstance(
              vk::InstanceCreateInfo()
                  .setPApplicationInfo(&app)
                  .setEnabledExtensionCount(usedInstanceExtensions.size())
                  .setPpEnabledExtensionNames(usedInstanceExtensions.data())
                  .setEnabledLayerCount(usedLayers.size())
                  .setPpEnabledLayerNames(usedLayers.data())
              );
  
  enum_GPUs();
}

void free_depth_buffer()
{
  vktools::destroy_handle(depth_buffer.view, device);
  vktools::destroy_handle(depth_buffer.image, device);
  vktools::destroy_handle(depth_buffer.memory, device);
}

void free_swapchain_views()
{
  for (auto handle: swapchain_views)
    vktools::destroy_handle(handle, device);
  swapchain_views.clear();
}

void  free_resources()
{
  printf("v3d::free_resources\n");
  free_swapchain_views();
  free_depth_buffer();
  vktools::destroy_handle(swapchain, device);
}

void  shutdown()
{
  printf("v3d::shutdown\n");
  active_GPU = -1;
  system_GPUs.clear();

  vktools::destroy_handle(device);
  vktools::destroy_handle(instance);
}

vk::Instance&   get_vk()
{
  return instance;
}

static void choose_GPU(VkSurfaceKHR surface)
{
  const vk::QueueFlags  requiredQueueFlags (vk::QueueFlagBits::eGraphics | 
                                            vk::QueueFlagBits::eTransfer);
  int integratedGPUidx = -1;
  int discreteGPUidx = -1;

  for (size_t i = 0; i < system_GPUs.size(); ++i)
  {
    GPUInfo& gpuInfo = system_GPUs[i];
    printf("Testing GPU %s...\n", gpuInfo.props.deviceName);
    for (size_t qi = 0; qi < gpuInfo.queueFamilies.size(); ++qi)
    {
      vk::QueueFamilyProperties const& qfam = gpuInfo.queueFamilies[qi];
      if ((qfam.queueFlags & requiredQueueFlags) == requiredQueueFlags)
      {
        if (gpuInfo.device.getSurfaceSupportKHR(qi, surface))
        {
          gpuInfo.renderQueueFamilyIdx = qi;
          if (gpuInfo.props.deviceType == vk::PhysicalDeviceType::eDiscreteGpu)
            discreteGPUidx = i;
          else if (gpuInfo.props.deviceType == vk::PhysicalDeviceType::eIntegratedGpu)
            integratedGPUidx = i;
          break;
        }
      }
    }
  }

  if (integratedGPUidx < 0 && discreteGPUidx < 0)
    throw vulkan_error("failed to find suitable GPU");
 
  active_GPU = discreteGPUidx >= 0 ? discreteGPUidx : integratedGPUidx; 
  GPUInfo const& gpuInfo = system_GPUs[active_GPU];
   
  printf("Use GPU %s\n", gpuInfo.props.deviceName);

  const float one = 1.0f;
  vk::DeviceQueueCreateInfo queueCreateInfo;
  queueCreateInfo.setQueueCount(1);
  queueCreateInfo.setQueueFamilyIndex(gpuInfo.renderQueueFamilyIdx);
  queueCreateInfo.setPQueuePriorities(&one);

  auto deviceExtensions = choose_device_extensions(gpuInfo);
  device = gpuInfo.device.createDevice(
              vk::DeviceCreateInfo()
                .setEnabledExtensionCount(deviceExtensions.size())
                .setPpEnabledExtensionNames(deviceExtensions.data())
                .setQueueCreateInfoCount(1)
                .setPQueueCreateInfos(&queueCreateInfo)
           );

}

static void create_swap_chain()
{
  GPUInfo const& gpuInfo = system_GPUs[active_GPU];
  vk::PhysicalDevice const& dev = gpuInfo.device;
  vk::SurfaceCapabilitiesKHR surfCaps = dev.getSurfaceCapabilitiesKHR(window_surface);
  printf("\nsurface capabilities:\n");
  printf("\tminImageCount %d, maxImageCount %d\n", surfCaps.minImageCount, surfCaps.maxImageCount);
  printf("\tcurrentExtent %dx%d\n", surfCaps.currentExtent.width, surfCaps.currentExtent.height);
  printf("\tminImageExtent %dx%d\n", surfCaps.minImageExtent.width, surfCaps.minImageExtent.height);
  printf("\tmaxImageExtent %dx%d\n", surfCaps.maxImageExtent.width, surfCaps.maxImageExtent.height);
  printf("\tmaxImageArrayLayers %d\n", surfCaps.maxImageArrayLayers);

  auto surfFormats = dev.getSurfaceFormatsKHR(window_surface);

  int surfFormatIdx = -1;
  for (size_t i = 0; i < surfFormats.size(); ++i)
  {
    if (surfFormats[i].colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear &&
        surfFormats[i].format     == vk::Format::eB8G8R8A8Unorm)
    {
      surfFormatIdx = i;
      break;
    }
  }

  if (surfFormatIdx == -1)
    throw vulkan_error("failed to find suitable window surface format");
  
  vk::SurfaceFormatKHR  surfFormat = surfFormats[surfFormatIdx];

  auto presentModes = dev.getSurfacePresentModesKHR(window_surface);
  std::sort(presentModes.begin(), presentModes.end(),
      [] (vk::PresentModeKHR p1, vk::PresentModeKHR p2)
      {
        return present_mode_value(p1) > present_mode_value(p2);
      });
  vk::PresentModeKHR bestPm = presentModes.front();

  printf("Present mode %s choosen\n", vk::to_string(bestPm).c_str());
  swapchain = device.createSwapchainKHR(
                vk::SwapchainCreateInfoKHR()
                  .setPresentMode(bestPm)
                  .setSurface(window_surface)
                  .setImageFormat(surfFormat.format)
                  .setImageColorSpace(surfFormat.colorSpace)
                  .setImageExtent(surfCaps.currentExtent)
                  .setImageArrayLayers(1)
                  .setImageUsage(vk::ImageUsageFlagBits::eColorAttachment)
                  .setImageSharingMode(vk::SharingMode::eExclusive)
                  .setPreTransform(surfCaps.currentTransform)
                  .setCompositeAlpha(vk::CompositeAlphaFlagBitsKHR::eOpaque)
                  .setClipped(VK_TRUE)
                  .setMinImageCount(surfCaps.minImageCount + 1)
                  .setOldSwapchain(swapchain)
              );

  std::vector<vk::Image> swpImages = device.getSwapchainImagesKHR(swapchain);

  free_swapchain_views();
  for (vk::Image image: swpImages)
  {
    vk::ImageView view = device.createImageView(
           vk::ImageViewCreateInfo()
            .setImage(image)
            .setViewType(vk::ImageViewType::e2D)
            .setFormat(surfFormat.format)
            .setComponents(vk::ComponentMapping()
                              .setR(vk::ComponentSwizzle::eIdentity)
                              .setG(vk::ComponentSwizzle::eIdentity)
                              .setB(vk::ComponentSwizzle::eIdentity)
                              .setA(vk::ComponentSwizzle::eIdentity)
                          )
            .setSubresourceRange(vk::ImageSubresourceRange()
                                    .setAspectMask(vk::ImageAspectFlagBits::eColor)
                                    .setBaseMipLevel(0)
                                    .setLevelCount(1)
                                    .setBaseArrayLayer(0)
                                    .setLayerCount(1)
                          )
        );
    swapchain_views.push_back(view);
  }

  free_depth_buffer();
  depth_buffer.image = device.createImage(
        vk::ImageCreateInfo()
          .setImageType(vk::ImageType::e2D)
          .setFormat(vk::Format::eD16Unorm)
          .setExtent(vk::Extent3D()
                      .setWidth(surfCaps.currentExtent.width)
                      .setHeight(surfCaps.currentExtent.height)
                      .setDepth(1))
          .setMipLevels(1)
          .setArrayLayers(1)
          .setSamples(vk::SampleCountFlagBits::e1)
          .setTiling(vk::ImageTiling::eOptimal)
          .setUsage(vk::ImageUsageFlagBits::eDepthStencilAttachment)
          .setSharingMode(vk::SharingMode::eExclusive)
          .setInitialLayout(vk::ImageLayout::eUndefined)
      );

  vk::MemoryRequirements memReqs = device.getImageMemoryRequirements(depth_buffer.image);
  depth_buffer.memory = device.allocateMemory(vk::MemoryAllocateInfo()
                                            .setAllocationSize (memReqs.size)
                                            .setMemoryTypeIndex (find_memory_type(
                                                          memReqs.memoryTypeBits, 
                                                          vk::MemoryPropertyFlags()))
                                          );
  device.bindImageMemory(depth_buffer.image, depth_buffer.memory, 0);

  depth_buffer.view = device.createImageView(
           vk::ImageViewCreateInfo()
            .setImage(depth_buffer.image)
            .setViewType(vk::ImageViewType::e2D)
            .setFormat(vk::Format::eD16Unorm)
            .setComponents(vk::ComponentMapping()
                              .setR(vk::ComponentSwizzle::eIdentity)
                              .setG(vk::ComponentSwizzle::eIdentity)
                              .setB(vk::ComponentSwizzle::eIdentity)
                              .setA(vk::ComponentSwizzle::eIdentity)
                          )
            .setSubresourceRange(vk::ImageSubresourceRange()
                                    .setAspectMask(vk::ImageAspectFlagBits::eDepth)
                                    .setBaseMipLevel(0)
                                    .setLevelCount(1)
                                    .setBaseArrayLayer(0)
                                    .setLayerCount(1)
                          )
        );
}

void  on_window_create(VkSurfaceKHR surface)
{
  choose_GPU(surface);
  on_window_resize(surface);
}

void  on_window_resize(VkSurfaceKHR surface)
{
  window_surface = surface;
  create_swap_chain();
}

void  on_device_lost()
{
}

} // namespace v3d

