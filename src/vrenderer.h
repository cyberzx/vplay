#pragma once
#include  <vector>
#include  <unordered_map>
#include  <vulkan/vulkan.h>
#include  <vulkan/vk_sdk_platform.h>

class VKRenderer
{
public:
  void  initInstance(const char* app_name, const char* engine_name);
  void  chooseGPU(VkSurfaceKHR window_surface);
  void  createSwapchain();

  void  onResize(VkSurfaceKHR window_surface);

  VkInstance  getVkInstance() const { return instance; }

private:
  struct GPUInfo
  {
    VkPhysicalDeviceProperties            props;
    VkPhysicalDeviceMemoryProperties      memoryProps;
    VkPhysicalDeviceFeatures              features;
    std::vector<VkExtensionProperties>    extensions;
    std::vector<VkQueueFamilyProperties>  queueFamilies;
  };

  std::vector<const char*>  chooseExtensions();
  std::vector<const char*>  chooseDeviceExtensions(GPUInfo& gpu);
  std::vector<const char*>  chooseLayers();

  void  collectGPUsInfo();
  void  createDepthbuffer();

  void  getMemoryType(uint32_t type_bits, VkFlags requirement_mask, uint32_t* type_index);

  bool  hasExtension(const char* ext_name,
                      std::vector<VkExtensionProperties> const& extensions) const;
  bool  hasLayer(const char* layer_name) const;

private:
  VkInstance                    instance  = VK_NULL_HANDLE;
  VkPhysicalDevice              activeGPU = VK_NULL_HANDLE;
  VkDevice                      device    = VK_NULL_HANDLE;

  uint32_t                graphicsQueueFamilyIdx = 0;

  std::unordered_map<VkPhysicalDevice, GPUInfo> systemGPUs;

  std::vector<VkLayerProperties>      layers;
  std::vector<VkExtensionProperties>  extensions;

  std::unordered_map<std::string, std::vector<VkExtensionProperties>>  layersExtensions;

  VkSurfaceKHR                  windowSurface; // not owned
  VkSwapchainKHR                swapchain = VK_NULL_HANDLE;
  VkFormat                      swapchainFormat;
  VkExtent2D                    swapchainExtent;
  std::vector<VkImage>          swapchainImages;
  std::vector<VkImageView>      swapchainViews;
  std::vector<VkFramebuffer>    swapchainFBs;
  std::vector<VkCommandBuffer>  commandBuffers;

  VkImage                       depthBufferImage;
  VkImageView                   depthBufferView;
  VkDeviceMemory                depthBufferMemory;

  VkPipelineLayout              pipelineLayout = VK_NULL_HANDLE;
  VkRenderPass                  renderPass = VK_NULL_HANDLE;
  VkPipeline                    graphicsPipeline = VK_NULL_HANDLE;
  VkCommandPool                 commandPool = VK_NULL_HANDLE;
  VkSemaphore                   imageAvailableSemaphore = VK_NULL_HANDLE;
  VkSemaphore                   renderFinishedSemaphore = VK_NULL_HANDLE;
};

