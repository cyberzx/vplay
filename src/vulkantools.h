#pragma once

#include "vulkan_api.h"

#include <utility>
#include <stdexcept>
#include <vector>
#include <type_traits>
#include <functional>

class vulkan_error : public std::runtime_error
{
public:
  vulkan_error(const char* msg) : std::runtime_error(std::string("[VULKAN] ") + msg)
  {
  }

  vulkan_error(std::string const& msg) : std::runtime_error("[VULKAN] " + msg)
  {
  }
};

namespace vktools {

inline std::string to_string(VkResult code)
{
  return vk::to_string(static_cast<vk::Result>(code));
}

template<typename Func, typename... Args>
void checked_call(Func&& func, const char* error_str, Args&&... args)
{
  VkResult err = func(std::forward<Args>(args)...);
  if (err != VK_SUCCESS)
  {
    std::string errText = error_str;
    errText += ": ";
    errText += vktools::to_string(err);
    throw vulkan_error(errText);
  }
}

template<typename vkEnumerateFunc, typename vkEnumObj, typename... Args>
auto get_vk_array(vkEnumerateFunc&& func, std::vector<vkEnumObj>& result, Args&&... args)
{
  uint32_t elemsNum = 0;
  typedef decltype(std::invoke(func, std::forward<Args>(args)..., nullptr, nullptr)) vkEnumerateFuncRet;
  if constexpr (std::is_void<vkEnumerateFuncRet>::value)
  {
    func(std::forward<Args>(args)..., &elemsNum, nullptr);
    result.resize(elemsNum);
    if (elemsNum > 0)
      func(std::forward<Args>(args)..., &elemsNum, result.data());
  }
  else
  {
    VkResult res = func(std::forward<Args>(args)..., &elemsNum, nullptr);
    if (res != VK_SUCCESS)
      return res;
    result.resize(elemsNum);
    if (elemsNum == 0)
      return VK_SUCCESS;
    return func(std::forward<Args>(args)..., &elemsNum, result.data());
  }
}

inline void device_destroy(vk::Semaphore& handle, vk::Device const& device)
{
  device.destroySemaphore(handle);
}

inline void device_destroy(vk::CommandPool& handle, vk::Device const& device)
{
  device.destroyCommandPool(handle);
}

inline void device_destroy(vk::Framebuffer& handle, vk::Device const& device)
{
  device.destroyFramebuffer(handle);
}

inline void device_destroy(vk::ShaderModule& handle, vk::Device const& device)
{
  device.destroyShaderModule(handle);
}

inline void device_destroy(vk::Pipeline& handle, vk::Device const& device)
{
  device.destroyPipeline(handle);
}

inline void device_destroy(vk::PipelineCache& handle, vk::Device const& device)
{
  device.destroyPipelineCache(handle);
}

inline void device_destroy(vk::PipelineLayout& handle, vk::Device const& device)
{
  device.destroyPipelineLayout(handle);
}

inline void device_destroy(vk::RenderPass& handle, vk::Device const& device)
{
  device.destroyRenderPass(handle);
}

inline void device_destroy(vk::DeviceMemory& handle, vk::Device const& device)
{
  device.freeMemory(handle);
}

inline void device_destroy(vk::Image& handle, vk::Device const& device)
{
  device.destroyImage(handle);
}

inline void device_destroy(vk::ImageView& handle, vk::Device const& device)
{
  device.destroyImageView(handle);
}

inline void device_destroy(vk::SwapchainKHR& handle, vk::Device const& device)
{
  device.destroySwapchainKHR(handle);
}

template<typename vkHandle>
void destroy_handle(vkHandle& handle, vk::Device const& device)
{
  if (handle)
  {
    device_destroy(handle, device);
    handle = VK_NULL_HANDLE;
  }
}

template<typename vkHandle>
void destroy_handle(vkHandle& handle)
{
  if (handle)
  {
    handle.destroy();
    handle = VK_NULL_HANDLE;
  }
}

} // namespace vktools
