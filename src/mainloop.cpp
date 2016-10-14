#include <vulkan/vulkan.h>
#include <vulkan/vk_sdk_platform.h>

#include <X11/Xutil.h>
#include <xcb/xcb.h>

#include <stdio.h>
#include <string.h>

#include <stdexcept>
#include <vector>
#include <functional>
#include <algorithm>
#include <stdio.h>

#include "vulkantools.h"
#include "vrenderer.h"

Display* display;
Window xlib_window;
Atom xlib_wm_delete_window;

xcb_connection_t *connection;
xcb_screen_t *screen;
xcb_window_t window;
xcb_intern_atom_reply_t *atom_wm_delete_window;

// ---------------------------------------------
// VULKAN data
// ---------------------------------------------
VkInstance        vk;
VkPhysicalDevice  gpu;
VkDevice          device;
VkQueue           graphicsQueue;
VkSurfaceKHR      xcbSurface;

VkSwapchainKHR    xcbSwapchain;
VkFormat          swapchainFormat;
VkExtent2D        swapchainExtent;
VkPipelineLayout  pipelineLayout;
VkRenderPass      renderPass;
VkPipeline        graphicsPipeline;
VkCommandPool     commandPool;

VkSemaphore       imageAvailableSemaphore;
VkSemaphore       renderFinishedSemaphore;

std::vector<VkImageView>    swapchainViews;
std::vector<VkImage>        swapchainImages;
std::vector<VkFramebuffer>  swapchainFBs;
std::vector<VkCommandBuffer>  commandBuffers;

int               queueFamilyIdx = -1;

int               width = 800;
int               height = 600;

bool              quit = false;

std::vector<char>   readFile(const char* fname)
{
  std::vector<char> content;
  FILE* f = fopen(fname, "r");
  if (f == nullptr)
    throw std::runtime_error("failed to open shader file");
  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fseek(f, 0, SEEK_SET);
  content.resize(size);
  fread(content.data(), size, 1, f);
  fclose(f);
  return content;
}

void init_vk()
{
  std::vector<VkLayerProperties>  layProps;
  if (!vktools::get_vk_array(vkEnumerateInstanceLayerProperties, layProps))
    throw std::runtime_error("failed to get layer properties");

  std::vector<VkExtensionProperties>  extProps;
  if (!vktools::get_vk_array(vkEnumerateInstanceExtensionProperties, extProps, nullptr))
    throw std::runtime_error("failed to get extension properties");

  if (extProps.size() > 0)
  {
    printf("Found %u extension properties\n", extProps.size());
    for (VkExtensionProperties const& props: extProps)
      printf("%s, ver %u\n", props.extensionName, props.specVersion);
    printf("\n");
  }

  if (layProps.size() > 0)
  {
    printf("Found %u layer properties\n", layProps.size());
    for (VkLayerProperties const& props: layProps)
    {
      printf("%s, ver %u, impl %d: %s\n", props.layerName, props.specVersion,
                                      props.implementationVersion, props.description);

      std::vector<VkExtensionProperties>  extLayProps;
      if (vktools::get_vk_array(vkEnumerateInstanceExtensionProperties, extLayProps, props.layerName))
      {
        for (VkExtensionProperties const& props: extLayProps)
          printf("\t%s, ver %u\n", props.extensionName, props.specVersion);
      }
    }
    printf("\n");
  }

  std::vector<const char*> extensions;
  for (VkExtensionProperties const& props: extProps)
  {
    if (!strcmp(props.extensionName, VK_KHR_SURFACE_EXTENSION_NAME) ||
        !strcmp(props.extensionName, VK_KHR_XCB_SURFACE_EXTENSION_NAME) ||
        !strcmp(props.extensionName, VK_EXT_DEBUG_REPORT_EXTENSION_NAME))
    {
      extensions.push_back(props.extensionName);
    }
  }

  std::vector<const char*> layers;
  for (VkLayerProperties const& props: layProps)
  {
    if (!strcmp(props.layerName, "VK_LAYER_LUNARG_core_validation") ||
        !strcmp(props.layerName, "VK_LAYER_LUNARG_standard_validation"))
    {
      layers.push_back(props.layerName);
    }
  }

  const VkApplicationInfo app = {
    .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
    .pNext = nullptr,
    .pApplicationName = "vplay",
    .applicationVersion = 0,
    .pEngineName = "vplay",
    .engineVersion = 0,
    .apiVersion = VK_API_VERSION_1_0,
  };

  VkInstanceCreateInfo instInfo = {
    .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
    .pNext = nullptr,
    .flags = 0,
    .pApplicationInfo = &app,
    .enabledLayerCount = (uint32_t)layers.size(),
    .ppEnabledLayerNames = layers.empty() ? nullptr : layers.data(),
    .enabledExtensionCount = (uint32_t)extensions.size(),
    .ppEnabledExtensionNames = extensions.empty() ? nullptr : extensions.data(),
  };

  vktools::checked_call(vkCreateInstance, "failed to create vulkan instance",
                        &instInfo, nullptr, &vk);

  std::vector<VkPhysicalDevice> physDevices;
  if (!vktools::get_vk_array(vkEnumeratePhysicalDevices, physDevices, vk))
    throw std::runtime_error("failed to get physical devices");

  if (physDevices.size() > 0)
  {
    printf("Found %u devices\n", physDevices.size());
    for (VkPhysicalDevice device: physDevices)
    {
      VkPhysicalDeviceProperties  props;
      vkGetPhysicalDeviceProperties(device, &props);
      printf("%s\n", props.deviceName);
    }
    gpu = physDevices.front();
  }

  std::vector<VkQueueFamilyProperties>  queueFamiliesProperties;
  if (!vktools::get_vk_array(vkGetPhysicalDeviceQueueFamilyProperties, queueFamiliesProperties, gpu))
    throw std::runtime_error("failed to get queue families");

  for (size_t i = 0; i < queueFamiliesProperties.size(); ++i)
  {
    if (queueFamiliesProperties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
    {
      VkBool32 presentSupport = false;
      vkGetPhysicalDeviceSurfaceSupportKHR(gpu, i, xcbSurface, &presentSupport);
      if (!presentSupport)
        continue;

      queueFamilyIdx = i;
      break;
    }
  }

  if (queueFamilyIdx < 0)
    throw std::runtime_error("failed to find graphics queue family");

  float queuePriorities = 1.0f;
  VkDeviceQueueCreateInfo deviceQueueCreateInfo = {
    .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
    .pNext = nullptr,
    .flags = 0,
    .queueFamilyIndex = static_cast<uint32_t>(queueFamilyIdx),
    .queueCount = 1,
    .pQueuePriorities = &queuePriorities
  };

  std::vector<VkExtensionProperties>  deviceExtProps;
  if (!vktools::get_vk_array(vkEnumerateDeviceExtensionProperties, deviceExtProps, gpu, nullptr))
    throw std::runtime_error("failed to get device extensions");

  extensions.clear();
  for (VkExtensionProperties const& props: deviceExtProps)
  {
    if (!strcmp(props.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) ||
        !strcmp(props.extensionName, VK_NV_GLSL_SHADER_EXTENSION_NAME))
    {
      extensions.push_back(props.extensionName);
    }
  }

  VkDeviceCreateInfo deviceCreateInfo = {
    .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
    .pNext = nullptr,
    .flags = 0,
    .queueCreateInfoCount = 1,
    .pQueueCreateInfos = &deviceQueueCreateInfo,
    .enabledLayerCount = 0,
    .ppEnabledLayerNames = nullptr,
    .enabledExtensionCount = (uint32_t)extensions.size(),
    .ppEnabledExtensionNames = extensions.empty() ? nullptr : extensions.data(),
    .pEnabledFeatures = nullptr
  };

  vktools::checked_call(vkCreateDevice, "failed to create logical device",
                        gpu, &deviceCreateInfo, nullptr, &device);

  vkGetDeviceQueue(device, queueFamilyIdx, 0, &graphicsQueue);
}

void init_xcb()
{
  int scr;
  connection = xcb_connect(nullptr, &scr);
  if (xcb_connection_has_error(connection) > 0)
    throw std::runtime_error("cannot find a compatable vulkan installable client driver (ICD)\nExitting ...\n");

  const xcb_setup_t  *setup = xcb_get_setup(connection);
  xcb_screen_iterator_t   iter = xcb_setup_roots_iterator(setup);

  while (scr-- > 0)
    xcb_screen_next(&iter);
  screen = iter.data;

  // create window
  window = xcb_generate_id(connection);
  uint32_t value_mask, value_list[32];

  value_mask = XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK;
  value_list[0] = screen->black_pixel;
  value_list[1] = XCB_EVENT_MASK_KEY_RELEASE | XCB_EVENT_MASK_EXPOSURE |
                  XCB_EVENT_MASK_STRUCTURE_NOTIFY;

  xcb_create_window(connection, XCB_COPY_FROM_PARENT, window,
                    screen->root, 0, 0, width, height, 0,
                    XCB_WINDOW_CLASS_INPUT_OUTPUT, screen->root_visual,
                    value_mask, value_list);


  xcb_intern_atom_cookie_t cookie =
      xcb_intern_atom(connection, 1, 12, "WM_PROTOCOLS");
  xcb_intern_atom_reply_t *reply =
      xcb_intern_atom_reply(connection, cookie, 0);

  xcb_intern_atom_cookie_t cookie2 =
      xcb_intern_atom(connection, 0, 16, "WM_DELETE_WINDOW");
  atom_wm_delete_window =
      xcb_intern_atom_reply(connection, cookie2, 0);

  xcb_change_property(connection, XCB_PROP_MODE_REPLACE, window,
                      (*reply).atom, 4, 32, 1,
                      &(*atom_wm_delete_window).atom);
  free(reply);

  xcb_map_window(connection, window);

  // create swap chain
  VkXcbSurfaceCreateInfoKHR surfaceCreateInfo = {
    .sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR,
    .pNext = nullptr,
    .flags = 0,
    .connection = connection,
    .window = window
  };

  vktools::checked_call(vkCreateXcbSurfaceKHR, "failed to create xcb surface",
                        vk, &surfaceCreateInfo, nullptr, &xcbSurface);

  VkBool32 surfSupported = VK_FALSE;
  vktools::checked_call(vkGetPhysicalDeviceSurfaceSupportKHR, "failed to get surface support flag",
                        gpu, queueFamilyIdx, xcbSurface, &surfSupported);

  if (!surfSupported)
    throw std::runtime_error("surface is not supported by device");

  VkSurfaceCapabilitiesKHR surfCaps;
  vktools::checked_call(vkGetPhysicalDeviceSurfaceCapabilitiesKHR, "failed to get surface caps",
                        gpu, xcbSurface, &surfCaps);

  printf("\nsurface capabilities:\n");
  printf("\tminImageCount %d, maxImageCount %d\n", surfCaps.minImageCount, surfCaps.maxImageCount);
  printf("\tcurrentExtent %dx%d\n", surfCaps.currentExtent.width, surfCaps.currentExtent.height);
  printf("\tminImageExtent %dx%d\n", surfCaps.minImageExtent.width, surfCaps.minImageExtent.height);
  printf("\tmaxImageExtent %dx%d\n", surfCaps.maxImageExtent.width, surfCaps.maxImageExtent.height);
  printf("\tmaxImageArrayLayers %d\n", surfCaps.maxImageArrayLayers);

  std::vector<VkSurfaceFormatKHR> surfFormats;
  if (!vktools::get_vk_array(vkGetPhysicalDeviceSurfaceFormatsKHR, surfFormats, gpu, xcbSurface))
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

  if (surfFormatIdx == -1)
    throw std::runtime_error("failed to find suitable surface format");

  std::vector<VkPresentModeKHR> presentModes;
  if (!vktools::get_vk_array(vkGetPhysicalDeviceSurfacePresentModesKHR, presentModes, gpu, xcbSurface))
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
    .surface = xcbSurface,
    .minImageCount = surfCaps.minImageCount,
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
    .oldSwapchain = VK_NULL_HANDLE
  };


  vktools::checked_call(vkCreateSwapchainKHR, "failed to create swapchain",
                        device, &swapchainCreateInfo,
                        nullptr, &xcbSwapchain);

  if (!vktools::get_vk_array(vkGetSwapchainImagesKHR, swapchainImages, device, xcbSwapchain))
    throw std::runtime_error("failed to retrieve swapchain images");

  swapchainFormat = swapchainCreateInfo.imageFormat;
  swapchainExtent = swapchainCreateInfo.imageExtent;

  for (VkImage image: swapchainImages)
  {
    VkImageViewCreateInfo imageCreateInfo = {
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
                          device, &imageCreateInfo, nullptr, &imageView);
    swapchainViews.push_back(imageView);
  }
}

VkShaderModule createShader(const std::vector<char>& code)
{
  VkShaderModuleCreateInfo shaderModuleCreateInfo = {
    .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
    .pNext = nullptr,
    .flags = 0,
    .codeSize = code.size(),
    .pCode = (uint32_t const*)code.data(),
  };

  VkShaderModule shaderModule;
  vktools::checked_call(vkCreateShaderModule, "failed to create shader",
                        device, &shaderModuleCreateInfo, nullptr, &shaderModule);
  return shaderModule;
}

void  create_pipeline()
{
  VkShaderModule vertShader = createShader(readFile("vert.spv"));
  VkShaderModule fragShader = createShader(readFile("frag.spv"));

  VkPipelineShaderStageCreateInfo vertShaderStageCreateInfo = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
    .pNext = nullptr,
    .flags = 0,
    .stage = VK_SHADER_STAGE_VERTEX_BIT,
    .module = vertShader,
    .pName = "main",
    .pSpecializationInfo = nullptr
  };

  VkPipelineShaderStageCreateInfo fragShaderStageCreateInfo = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
    .pNext = nullptr,
    .flags = 0,
    .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
    .module = fragShader,
    .pName = "main",
    .pSpecializationInfo = nullptr
  };

  VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageCreateInfo,
                                                    fragShaderStageCreateInfo};

  VkPipelineVertexInputStateCreateInfo vertexInputInfo = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
    .pNext = nullptr,
    .flags = 0,
    .vertexBindingDescriptionCount = 0,
    .pVertexBindingDescriptions = nullptr,
    .vertexAttributeDescriptionCount = 0,
    .pVertexAttributeDescriptions = nullptr,
  };

  VkPipelineInputAssemblyStateCreateInfo inputAssemblyInfo = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
    .pNext = nullptr,
    .flags = 0,
    .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    .primitiveRestartEnable = VK_FALSE,
  };

  VkViewport viewport = {
    .x = 0,
    .y = 0,
    .width = (float)swapchainExtent.width,
    .height= (float)swapchainExtent.height,
    .minDepth = 0,
    .maxDepth = 1
  };

  VkRect2D scissors = {
    .offset = {0, 0},
    .extent = swapchainExtent
  };

  VkPipelineViewportStateCreateInfo viewportStateInfo = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
    .pNext = nullptr,
    .flags = 0,
    .viewportCount = 0,
    .pViewports = &viewport,
    .scissorCount = 0,
    .pScissors = &scissors
  };

  VkPipelineRasterizationStateCreateInfo rasterizerInfo = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
    .pNext = nullptr,
    .flags = 0,
    .depthClampEnable = VK_FALSE,
    .rasterizerDiscardEnable = VK_FALSE,
    .polygonMode = VK_POLYGON_MODE_FILL,
    .cullMode = VK_CULL_MODE_BACK_BIT,
    .frontFace = VK_FRONT_FACE_CLOCKWISE,
    .depthBiasEnable = VK_FALSE,
    .depthBiasConstantFactor = 0.0f,
    .depthBiasClamp = 0.0f,
    .depthBiasSlopeFactor = 0.0f,
    .lineWidth = 1.0f
  };

  VkPipelineMultisampleStateCreateInfo multisamplingInfo = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
    .pNext = nullptr,
    .flags = 0,
    .rasterizationSamples= VK_SAMPLE_COUNT_1_BIT,
    .sampleShadingEnable = VK_FALSE,
    .minSampleShading    = 1.0f,
    .pSampleMask         = nullptr,
    .alphaToCoverageEnable = VK_FALSE,
    .alphaToOneEnable    = VK_FALSE
  };

  VkPipelineColorBlendAttachmentState colorBlendAttachment = {
    .blendEnable = VK_FALSE,
    .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
    .dstColorBlendFactor = VK_BLEND_FACTOR_ZERO,
    .colorBlendOp = VK_BLEND_OP_ADD,
    .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
    .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
    .alphaBlendOp = VK_BLEND_OP_ADD,
    .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
  };

  VkPipelineColorBlendStateCreateInfo colorBlendStateInfo = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
    .pNext = nullptr,
    .flags = 0,
    .logicOpEnable = VK_FALSE,
    .logicOp = VK_LOGIC_OP_COPY,
    .attachmentCount = 1,
    .pAttachments = &colorBlendAttachment,
    .blendConstants = {0.0f, 0.0f, 0.0f, 0.0f}
  };

  VkPipelineLayoutCreateInfo pipelineLayoutInfo = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
    .pNext = nullptr,
    .flags = 0,
    .setLayoutCount = 0,
    .pSetLayouts = nullptr,
    .pushConstantRangeCount = 0,
    .pPushConstantRanges = 0
  };

  vktools::checked_call(vkCreatePipelineLayout, "failed to create pipeline layout",
                        device, &pipelineLayoutInfo, nullptr, &pipelineLayout);

  VkAttachmentDescription colorAttachment = {
    .flags = 0,
    .format = swapchainFormat,
    .samples= VK_SAMPLE_COUNT_1_BIT,
    .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
    .storeOp= VK_ATTACHMENT_STORE_OP_STORE,
    .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
    .stencilStoreOp= VK_ATTACHMENT_STORE_OP_DONT_CARE,
    .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    .finalLayout   = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
  };

  VkAttachmentReference colorAttachmentRef = {
    .attachment = 0,
    .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
  };

  VkSubpassDescription subpass = {
    .flags = 0,
    .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
    .inputAttachmentCount = 0,
    .pInputAttachments = nullptr,
    .colorAttachmentCount = 1,
    .pColorAttachments = &colorAttachmentRef,
    .pResolveAttachments = nullptr,
    .pDepthStencilAttachment = nullptr,
    .preserveAttachmentCount = 0,
    .pPreserveAttachments = nullptr
  };

  VkSubpassDependency sdeps = {
    .srcSubpass = VK_SUBPASS_EXTERNAL,
    .dstSubpass = 0,
    .srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
    .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
    .srcAccessMask = VK_ACCESS_MEMORY_READ_BIT,
    .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
    .dependencyFlags = 0
  };

  VkRenderPassCreateInfo renderPassInfo = {
    .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
    .pNext = nullptr,
    .flags = 0,
    .attachmentCount = 1,
    .pAttachments = &colorAttachment,
    .subpassCount = 1,
    .pSubpasses = &subpass,
    .dependencyCount = 1,
    .pDependencies = &sdeps
  };

  vktools::checked_call(vkCreateRenderPass, "failed to create render pass",
                        device, &renderPassInfo, nullptr, &renderPass);


  VkGraphicsPipelineCreateInfo pipelineInfo = {
    .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
    .pNext = nullptr,
    .flags = 0,
    .stageCount = 2,
    .pStages = shaderStages,
    .pVertexInputState = &vertexInputInfo,
    .pInputAssemblyState = &inputAssemblyInfo,
    .pTessellationState = nullptr,
    .pViewportState = &viewportStateInfo,
    .pRasterizationState = &rasterizerInfo,
    .pMultisampleState = &multisamplingInfo,
    .pDepthStencilState = nullptr,
    .pColorBlendState = &colorBlendStateInfo,
    .pDynamicState = nullptr,
    .layout = pipelineLayout,
    .renderPass = renderPass,
    .subpass = 0,
    .basePipelineHandle = 0,
    .basePipelineIndex = -1
  };

  vktools::checked_call(vkCreateGraphicsPipelines, "failed to create pipeline",
                        device, nullptr, 1, &pipelineInfo, nullptr, &graphicsPipeline);

  vkDestroyShaderModule(device, fragShader, nullptr);
  vkDestroyShaderModule(device, vertShader, nullptr);

  for (VkImageView const& imgView: swapchainViews)
  {
    VkFramebufferCreateInfo fbCreateInfo = {
      .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
      .pNext = nullptr,
      .flags = 0,
      .renderPass = renderPass,
      .attachmentCount = 1,
      .pAttachments = &imgView,
      .width = swapchainExtent.width,
      .height= swapchainExtent.height,
      .layers= 1
    };

    VkFramebuffer fb;
    vktools::checked_call(vkCreateFramebuffer, "failed to create framebuffer",
                          device, &fbCreateInfo, nullptr, &fb);
    swapchainFBs.push_back(fb);
  }

  VkCommandPoolCreateInfo commandPoolInfo = {
    .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
    .pNext = nullptr,
    .flags = 0,
    .queueFamilyIndex = (uint32_t)queueFamilyIdx
  };


  vktools::checked_call(vkCreateCommandPool, "failed to create command pool",
                        device, &commandPoolInfo, nullptr, &commandPool);


  commandBuffers.resize(swapchainFBs.size());
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

void  record_command_buffers()
{
  for (size_t i = 0; i < commandBuffers.size(); ++i)
  {
    VkCommandBufferBeginInfo  begInfo = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      .pNext = nullptr,
      .flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT,
      .pInheritanceInfo = nullptr
    };

    vkBeginCommandBuffer(commandBuffers[i], &begInfo);

    VkClearValue clearColor = {0.0f, 0.0f, 0.0f, 1.0f};
    VkRenderPassBeginInfo   renderPassInfo = {
      .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
      .pNext = nullptr,
      .renderPass = renderPass,
      .framebuffer= swapchainFBs[i],
      .renderArea = {
        .offset = {0, 0},
        .extent = swapchainExtent
      },
      .clearValueCount = 1,
      .pClearValues = &clearColor
    };
    vkCmdBeginRenderPass(commandBuffers[i], &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);
    vkCmdDraw(commandBuffers[i], 3, 1, 0, 0);
    vkCmdEndRenderPass(commandBuffers[i]);

    vktools::checked_call(vkEndCommandBuffer, "failed to record command buffer",
                          commandBuffers[i]);
  }

  VkSemaphoreCreateInfo semCreateInfo = {
    .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
    .pNext = nullptr,
    .flags = 0
  };

  vktools::checked_call(vkCreateSemaphore, "failed to create semaphore",
                        device, &semCreateInfo, nullptr, &imageAvailableSemaphore);
  vktools::checked_call(vkCreateSemaphore, "failed to create semaphore",
                        device, &semCreateInfo, nullptr, &renderFinishedSemaphore);
}

void  draw()
{
  uint32_t imageIndex;
  vkAcquireNextImageKHR(device, xcbSwapchain, std::numeric_limits<uint64_t>::max(),
                        imageAvailableSemaphore, nullptr, &imageIndex);

  VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};

  VkSubmitInfo submitInfo = {
    .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
    .pNext = nullptr,
    .waitSemaphoreCount = 1,
    .pWaitSemaphores = &imageAvailableSemaphore,
    .pWaitDstStageMask = waitStages,
    .commandBufferCount = 1,
    .pCommandBuffers = &commandBuffers[imageIndex],
    .signalSemaphoreCount = 1,
    .pSignalSemaphores = &renderFinishedSemaphore
  };

  vktools::checked_call(vkQueueSubmit, "failed to submit graphics queue",
                        graphicsQueue, 1, &submitInfo, nullptr);

  VkPresentInfoKHR presentInfo = {
    .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
    .pNext = nullptr,
    .waitSemaphoreCount = 1,
    .pWaitSemaphores = &renderFinishedSemaphore,
    .swapchainCount = 1,
    .pSwapchains = &xcbSwapchain,
    .pImageIndices = &imageIndex,
    .pResults = nullptr
  };

  vkQueuePresentKHR(graphicsQueue, &presentInfo);
}

void  resize()
{
  printf("resize\n");
}

void  handle_window_event(const xcb_generic_event_t* event)
{
  switch (event->response_type & 0x7f) {
  case XCB_EXPOSE:
      draw();
      break;
  case XCB_CLIENT_MESSAGE:
      if ((*(xcb_client_message_event_t *)event).data.data32[0] ==
          (*atom_wm_delete_window).atom) {
          quit = true;
      }
      break;
  case XCB_KEY_RELEASE: {
      const xcb_key_release_event_t *key =
          (const xcb_key_release_event_t *)event;

      if (key->detail == 0x9)
          quit = true;
  } break;
  case XCB_DESTROY_NOTIFY:
      quit = true;
      break;
  case XCB_CONFIGURE_NOTIFY: {
      const xcb_configure_notify_event_t *cfg =
          (const xcb_configure_notify_event_t *)event;
      if ((width != cfg->width) || (height != cfg->height)) {
          width = cfg->width;
          height = cfg->height;
          resize();
      }
  } break;
  default:
      break;
  }
}

void window_loop()
{
  xcb_flush(connection);

  while (!quit)
  {
    if (xcb_generic_event_t* event = xcb_poll_for_event(connection))
    {
      handle_window_event(event);
      free(event);
    }

    draw();
    vkDeviceWaitIdle(device);
  }
}

int main2()
{
  try {
    init_vk();
    init_xcb();
    create_pipeline();
    record_command_buffers();
    window_loop();
  }
  catch (std::exception const& e)
  {
    printf("%s\n", e.what());
  }

  if (imageAvailableSemaphore)
    vkDestroySemaphore(device, imageAvailableSemaphore, nullptr);
  if (renderFinishedSemaphore)
    vkDestroySemaphore(device, renderFinishedSemaphore, nullptr);
  if (commandBuffers.size() > 0)
    vkFreeCommandBuffers(device, commandPool, commandBuffers.size(), commandBuffers.data());
  if (commandPool)
    vkDestroyCommandPool(device, commandPool, nullptr);
  for (VkFramebuffer fb: swapchainFBs)
    vkDestroyFramebuffer(device, fb, nullptr);
  if (graphicsPipeline)
    vkDestroyPipeline(device, graphicsPipeline, nullptr);
  if (renderPass)
    vkDestroyRenderPass(device, renderPass, nullptr);
  if (pipelineLayout)
    vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
  for (VkImageView imageView: swapchainViews)
    vkDestroyImageView(device, imageView, nullptr);
  if (xcbSwapchain)
    vkDestroySwapchainKHR(device, xcbSwapchain, nullptr);
  if (xcbSurface)
    vkDestroySurfaceKHR(vk, xcbSurface, nullptr);
  if (device)
    vkDestroyDevice(device, nullptr);
  vkDestroyInstance(vk, nullptr);

  xcb_destroy_window(connection, window);
  xcb_disconnect(connection);
  free(atom_wm_delete_window);
  return 0;
}
