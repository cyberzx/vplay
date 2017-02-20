#include "v3d.h"
#include "vulkantools.h"
#include "shaders.h"

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

struct SwapchainBuffer
{
  vk::Image          image;
  vk::ImageView      view;
  vk::CommandBuffer  cmd;
  vk::Framebuffer    framebuffer;
};

static std::vector<vk::LayerProperties>      layers;
static std::vector<vk::ExtensionProperties>  extensions;
static std::unordered_map<std::string, decltype(extensions)>  layers_extensions;

static vk::Instance   instance;
static vk::Device     device;
static vk::Queue      graphics_queue;
static vk::SwapchainKHR   swapchain;
static vk::Extent2D       swapchain_extent;
static vk::SurfaceFormatKHR  swapchain_format;

static std::vector<SwapchainBuffer>  swapchain_buffers;
static vk::Semaphore  image_acquired_semaphore;
static vk::Semaphore  render_finished_semaphore;

static struct 
{
  vk::Image          image;
  vk::ImageView      view;
  vk::DeviceMemory   memory;
} depth_buffer;

static vk::PipelineCache  pipeline_cache;
static vk::PipelineLayout pipeline_layout;
static vk::Pipeline       pipeline;
static vk::RenderPass     render_pass;

static vk::CommandPool    command_pool;
static std::vector<vk::CommandBuffer> command_buffers;

static std::vector<GPUInfo> system_GPUs;
static int active_GPU = -1;

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

static GPUInfo const& get_gpu()
{
  return system_GPUs[active_GPU];
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
  GPUInfo const& gpuInfo = get_gpu();
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
  for (SwapchainBuffer& buffer: swapchain_buffers)
  {
    vktools::destroy_handle(buffer.view, device);
    vktools::destroy_handle(buffer.framebuffer, device);
    device.freeCommandBuffers(command_pool, 1, &buffer.cmd);
  }
  swapchain_buffers.clear();
}

void  free_resources()
{
  printf("v3d::free_resources\n");
  free_swapchain_views();
  free_depth_buffer();

  vktools::destroy_handle(pipeline, device);
  vktools::destroy_handle(pipeline_cache, device);
  vktools::destroy_handle(pipeline_layout, device);
  vktools::destroy_handle(render_pass, device);
  vktools::destroy_handle(swapchain, device);
  vktools::destroy_handle(command_pool, device);
}

void  shutdown()
{
  printf("v3d::shutdown\n");
  active_GPU = -1;
  system_GPUs.clear();

  vktools::device_destroy(render_finished_semaphore, device);
  vktools::device_destroy(image_acquired_semaphore, device);
  vktools::destroy_handle(device);
  vktools::destroy_handle(instance);
}

vk::Instance&   get_vk()
{
  return instance;
}

vk::Device&   get_device()
{
  return  device;
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
  GPUInfo const& gpuInfo = get_gpu();
   
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

  graphics_queue = device.getQueue(gpuInfo.renderQueueFamilyIdx, 0);
}

static void  prepare_renderpass()
{
  const vk::AttachmentDescription attachments[] = {
    vk::AttachmentDescription()
               .setFormat(swapchain_format.format)
               .setSamples(vk::SampleCountFlagBits::e1)
               .setLoadOp(vk::AttachmentLoadOp::eClear)
               .setStoreOp(vk::AttachmentStoreOp::eStore)
               .setStencilLoadOp(vk::AttachmentLoadOp::eDontCare)
               .setStencilStoreOp(vk::AttachmentStoreOp::eDontCare)
               .setInitialLayout(vk::ImageLayout::eUndefined)
               .setFinalLayout(vk::ImageLayout::ePresentSrcKHR)
               ,
    vk::AttachmentDescription()
               .setFormat(vk::Format::eD16Unorm)
               .setSamples(vk::SampleCountFlagBits::e1)
               .setLoadOp(vk::AttachmentLoadOp::eClear)
               .setStoreOp(vk::AttachmentStoreOp::eStore)
               .setStencilLoadOp(vk::AttachmentLoadOp::eDontCare)
               .setStencilStoreOp(vk::AttachmentStoreOp::eDontCare)
               .setInitialLayout(vk::ImageLayout::eUndefined)
               .setFinalLayout(vk::ImageLayout::eDepthStencilAttachmentOptimal)
    };

  auto const colorRef = vk::AttachmentReference()
                        .setAttachment(0)
                        .setLayout(vk::ImageLayout::eColorAttachmentOptimal);

  auto const depthRef = vk::AttachmentReference()
                        .setAttachment(1)
                        .setLayout(vk::ImageLayout::eDepthStencilAttachmentOptimal);
  auto const subpass = vk::SubpassDescription()
                        .setPipelineBindPoint(vk::PipelineBindPoint::eGraphics)
                        .setInputAttachmentCount(0)
                        .setPInputAttachments(nullptr)
                        .setColorAttachmentCount(1)
                        .setPColorAttachments(&colorRef)
                        .setPResolveAttachments(nullptr)
                        .setPDepthStencilAttachment(&depthRef)
                        .setPreserveAttachmentCount(0)
                        .setPPreserveAttachments(nullptr);
  auto const dependency = vk::SubpassDependency()
                            .setSrcSubpass(VK_SUBPASS_EXTERNAL)
                            .setDstSubpass(0)
                            .setSrcStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput)
                            .setDstStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput)
                            .setDstAccessMask(vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite);
 
  render_pass = device.createRenderPass(vk::RenderPassCreateInfo()
                                          .setAttachmentCount(2)
                                          .setPAttachments(attachments)
                                          .setSubpassCount(1)
                                          .setPSubpasses(&subpass)
                                          .setDependencyCount(1)
                                          .setPDependencies(&dependency));
}

static void  prepare_descriptor_layout()
{
  pipeline_layout = device.createPipelineLayout(
                      vk::PipelineLayoutCreateInfo()
                    );
}

static void  prepare_command_pool()
{
  command_pool = device.createCommandPool(vk::CommandPoolCreateInfo()
                              .setQueueFamilyIndex(get_gpu().renderQueueFamilyIdx));

  auto const cmdInfo = vk::CommandBufferAllocateInfo()
                        .setCommandPool(command_pool)
                        .setLevel(vk::CommandBufferLevel::ePrimary)
                        .setCommandBufferCount(1);

  for (SwapchainBuffer& buffer: swapchain_buffers)
    buffer.cmd = device.allocateCommandBuffers(cmdInfo).front();
}

static void   write_command_buffers()
{
  vk::ClearValue const clearValues[2] = {
      vk::ClearColorValue(std::array<float, 4>({{0.5f, 0.2f, 0.2f, 0.2f}})),
      vk::ClearDepthStencilValue(1.0f, 0u)};


  auto const viewport = vk::Viewport()
                            .setWidth((float)swapchain_extent.width)
                            .setHeight((float)swapchain_extent.height)
                            .setMinDepth((float)0.0f)
                            .setMaxDepth((float)1.0f);

  vk::Rect2D const scissor(vk::Offset2D(0, 0),
                           swapchain_extent);

  for (SwapchainBuffer& buffer: swapchain_buffers)
  {
    vk::CommandBuffer& cmd = buffer.cmd;
    cmd.begin(vk::CommandBufferBeginInfo()
                   .setFlags(vk::CommandBufferUsageFlagBits::eSimultaneousUse));

    cmd.beginRenderPass(vk::RenderPassBeginInfo()
                              .setFramebuffer(buffer.framebuffer)
                              .setRenderPass(render_pass)
                              .setClearValueCount(2)
                              .setPClearValues(clearValues)
                              .setRenderArea(
                                  vk::Rect2D(vk::Offset2D(0, 0),
                                  swapchain_extent
                                ))
                             ,vk::SubpassContents::eInline);
    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
    cmd.setViewport(0, 1, &viewport);
    cmd.setScissor(0, 1, &scissor);
    cmd.draw(3, 1, 0, 0);
    cmd.endRenderPass();
    cmd.end();
  }
}

static void  prepare_framebuffers()
{
  vk::ImageView  attachments[2];
  attachments[1] = depth_buffer.view;

  auto const fbInfo = vk::FramebufferCreateInfo()
                        .setRenderPass(render_pass)
                        .setAttachmentCount(2)
                        .setPAttachments(attachments)
                        .setWidth(swapchain_extent.width)
                        .setHeight(swapchain_extent.height)
                        .setLayers(1);

  for (auto& buffer: swapchain_buffers)
  {
    attachments[0] = buffer.view;
    buffer.framebuffer = device.createFramebuffer(fbInfo);
  }
}

static void  prepare_pipeline()
{
  pipeline_cache = device.createPipelineCache(vk::PipelineCacheCreateInfo());
  vk::PipelineVertexInputStateCreateInfo const vertexInputInfo;

  auto const inputAssemblyInfo =
      vk::PipelineInputAssemblyStateCreateInfo().setTopology(
          vk::PrimitiveTopology::eTriangleList);

  auto const viewportInfo = vk::PipelineViewportStateCreateInfo()
                                .setViewportCount(1)
                                .setScissorCount(1);

  auto const rasterizationInfo =
      vk::PipelineRasterizationStateCreateInfo()
          .setDepthClampEnable(VK_FALSE)
          .setRasterizerDiscardEnable(VK_FALSE)
          .setPolygonMode(vk::PolygonMode::eFill)
          .setCullMode(vk::CullModeFlagBits::eNone)
          .setFrontFace(vk::FrontFace::eCounterClockwise)
          .setDepthBiasEnable(VK_FALSE)
          .setLineWidth(1.0f);

  auto const multisampleInfo = vk::PipelineMultisampleStateCreateInfo();

  auto const stencilOp = vk::StencilOpState()
                             .setFailOp(vk::StencilOp::eKeep)
                             .setPassOp(vk::StencilOp::eKeep)
                             .setCompareOp(vk::CompareOp::eAlways);

  auto const depthStencilInfo =
      vk::PipelineDepthStencilStateCreateInfo()
          .setDepthTestEnable(VK_FALSE)
          .setDepthWriteEnable(VK_TRUE)
          .setDepthCompareOp(vk::CompareOp::eLessOrEqual)
          .setDepthBoundsTestEnable(VK_FALSE)
          .setStencilTestEnable(VK_FALSE)
          .setFront(stencilOp)
          .setBack(stencilOp);

  vk::PipelineColorBlendAttachmentState const colorBlendAttachments[1] = {
      vk::PipelineColorBlendAttachmentState().setColorWriteMask(
          vk::ColorComponentFlagBits::eR |
          vk::ColorComponentFlagBits::eG |
          vk::ColorComponentFlagBits::eB |
          vk::ColorComponentFlagBits::eA)};

  auto const colorBlendInfo = vk::PipelineColorBlendStateCreateInfo()
                                  .setAttachmentCount(1)
                                  .setPAttachments(colorBlendAttachments);

  vk::DynamicState const dynamicStates[2] = {vk::DynamicState::eViewport,
                                             vk::DynamicState::eScissor};

  auto const dynamicStateInfo = vk::PipelineDynamicStateCreateInfo()
                                    .setPDynamicStates(dynamicStates)
                                    .setDynamicStateCount(2);

  auto vertShaderModule = load_shader_from_file("vert.spv");
  auto fragShaderModule = load_shader_from_file("frag.spv");

  vk::PipelineShaderStageCreateInfo const shaderStageInfo[2] = {
      vk::PipelineShaderStageCreateInfo()
          .setStage(vk::ShaderStageFlagBits::eVertex)
          .setModule(vertShaderModule)
          .setPName("main"),
      vk::PipelineShaderStageCreateInfo()
          .setStage(vk::ShaderStageFlagBits::eFragment)
          .setModule(fragShaderModule)
          .setPName("main")};

  auto const pipelineInfo = vk::GraphicsPipelineCreateInfo()
                            .setStageCount(2)
                            .setPStages(shaderStageInfo)
                            .setPVertexInputState(&vertexInputInfo)
                            .setPInputAssemblyState(&inputAssemblyInfo)
                            .setPViewportState(&viewportInfo)
                            .setPRasterizationState(&rasterizationInfo)
                            .setPMultisampleState(&multisampleInfo)
                            .setPDepthStencilState(&depthStencilInfo)
                            .setPColorBlendState(&colorBlendInfo)
                            .setPDynamicState(&dynamicStateInfo)
                            .setLayout(pipeline_layout)
                            .setRenderPass(render_pass);

  pipeline = device.createGraphicsPipeline(pipeline_cache, pipelineInfo);

  vktools::destroy_handle(vertShaderModule, device);
  vktools::destroy_handle(fragShaderModule, device);
}

static void create_semaphores()
{
  vk::SemaphoreCreateInfo  semCreateInfo;
  image_acquired_semaphore = device.createSemaphore(semCreateInfo); 
  render_finished_semaphore = device.createSemaphore(semCreateInfo);
}

static void create_swap_chain(VkSurfaceKHR surface)
{
  GPUInfo const& gpuInfo = get_gpu();
  vk::PhysicalDevice const& dev = gpuInfo.device;
  vk::SurfaceCapabilitiesKHR surfCaps = dev.getSurfaceCapabilitiesKHR(surface);
  printf("\nsurface capabilities:\n");
  printf("\tminImageCount %d, maxImageCount %d\n", surfCaps.minImageCount, surfCaps.maxImageCount);
  printf("\tcurrentExtent %dx%d\n", surfCaps.currentExtent.width, surfCaps.currentExtent.height);
  printf("\tminImageExtent %dx%d\n", surfCaps.minImageExtent.width, surfCaps.minImageExtent.height);
  printf("\tmaxImageExtent %dx%d\n", surfCaps.maxImageExtent.width, surfCaps.maxImageExtent.height);
  printf("\tmaxImageArrayLayers %d\n", surfCaps.maxImageArrayLayers);
  swapchain_extent = surfCaps.currentExtent;

  auto surfFormats = dev.getSurfaceFormatsKHR(surface);

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
  
  swapchain_format = surfFormats[surfFormatIdx];
  vk::SurfaceFormatKHR const& surfFormat = swapchain_format;

  auto presentModes = dev.getSurfacePresentModesKHR(surface);
  std::sort(presentModes.begin(), presentModes.end(),
      [] (vk::PresentModeKHR p1, vk::PresentModeKHR p2)
      {
        return present_mode_value(p1) > present_mode_value(p2);
      });
  vk::PresentModeKHR bestPm = presentModes.front();

  printf("Present mode %s choosen\n", vk::to_string(bestPm).c_str());
  vk::SwapchainKHR oldSwapchain = swapchain;
  swapchain = device.createSwapchainKHR(
                vk::SwapchainCreateInfoKHR()
                  .setPresentMode(bestPm)
                  .setSurface(surface)
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
                  .setOldSwapchain(oldSwapchain)
              );

  vktools::destroy_handle(oldSwapchain, device);

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
    swapchain_buffers.push_back({image, view});
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
  create_swap_chain(surface);
  create_semaphores();
  prepare_descriptor_layout();
  prepare_renderpass();
  prepare_pipeline();
  prepare_framebuffers();
  prepare_command_pool();
  write_command_buffers();
}

void  on_window_resize(VkSurfaceKHR surface)
{
  printf("on_window_resize\n");

  GPUInfo const& gpuInfo = get_gpu();
  if (!gpuInfo.device.getSurfaceSupportKHR(gpuInfo.renderQueueFamilyIdx, surface))
    throw vulkan_error("window surface does not support present via active gpu");

  //create_swap_chain(surface);
}

void  on_device_lost()
{
}

void render()
{
  uint32_t curBuffer = device.acquireNextImageKHR(swapchain, 
                                            UINT64_MAX, image_acquired_semaphore,
                                            VK_NULL_HANDLE).value;

  vk::PipelineStageFlags const stageFlags =
      vk::PipelineStageFlagBits::eColorAttachmentOutput;

  auto const submitInfo =
      vk::SubmitInfo()
          .setPWaitDstStageMask(&stageFlags)
          .setWaitSemaphoreCount(1)
          .setPWaitSemaphores(&image_acquired_semaphore)
          .setCommandBufferCount(1)
          .setPCommandBuffers(&swapchain_buffers[curBuffer].cmd)
          .setSignalSemaphoreCount(1)
          .setPSignalSemaphores(&render_finished_semaphore);
  graphics_queue.submit(1, &submitInfo, vk::Fence());

  auto const presentInfo = 
     vk::PresentInfoKHR()
      .setWaitSemaphoreCount(1)
      .setPWaitSemaphores(&render_finished_semaphore)
      .setSwapchainCount(1)
      .setPSwapchains(&swapchain)
      .setPImageIndices(&curBuffer);
  graphics_queue.presentKHR(presentInfo);
}

} // namespace v3d

