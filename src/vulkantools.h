#pragma once
#include <vulkan/vulkan.h>
#include <utility>
#include <stdexcept>

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

inline const char* to_string(VkResult errorCode)
{
  switch ((int)errorCode)
  {
#define TEST_CODE(r) case VK_ ##r: return "VK_" #r
    TEST_CODE(SUCCESS);
    TEST_CODE(NOT_READY);
    TEST_CODE(TIMEOUT);
    TEST_CODE(EVENT_SET);
    TEST_CODE(EVENT_RESET);
    TEST_CODE(INCOMPLETE);
    TEST_CODE(ERROR_OUT_OF_HOST_MEMORY);
    TEST_CODE(ERROR_OUT_OF_DEVICE_MEMORY);
    TEST_CODE(ERROR_INITIALIZATION_FAILED);
    TEST_CODE(ERROR_DEVICE_LOST);
    TEST_CODE(ERROR_MEMORY_MAP_FAILED);
    TEST_CODE(ERROR_LAYER_NOT_PRESENT);
    TEST_CODE(ERROR_EXTENSION_NOT_PRESENT);
    TEST_CODE(ERROR_FEATURE_NOT_PRESENT);
    TEST_CODE(ERROR_INCOMPATIBLE_DRIVER);
    TEST_CODE(ERROR_TOO_MANY_OBJECTS);
    TEST_CODE(ERROR_FORMAT_NOT_SUPPORTED);
    TEST_CODE(ERROR_FRAGMENTED_POOL);
    TEST_CODE(ERROR_SURFACE_LOST_KHR);
    TEST_CODE(ERROR_NATIVE_WINDOW_IN_USE_KHR);
    TEST_CODE(SUBOPTIMAL_KHR);
    TEST_CODE(ERROR_OUT_OF_DATE_KHR);
    TEST_CODE(ERROR_INCOMPATIBLE_DISPLAY_KHR);
    TEST_CODE(ERROR_VALIDATION_FAILED_EXT);
    TEST_CODE(ERROR_INVALID_SHADER_NV);
#undef TEST_CODE
  default:
    return "UNKNOWN";
  }
}

template<class Func, class... Args>
void checked_call(Func func, const char* error_str, Args... args)
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

template<typename... Args, typename vkEnumObj, typename vkEnumerateFunc>
requires requires (vkEnumerateFunc func, Args... args) { {func(args..., 0, 0)} -> void; }
bool get_vk_array(vkEnumerateFunc func, std::vector<vkEnumObj>& result, Args... args)
{
  uint32_t count = 0;
  func(args..., &count, nullptr);
  result.resize(count);
  if (count > 0)
    func(args..., &count, result.data());
  return true;
}

template<typename... Args, typename vkEnumObj, typename vkEnumerateFunc>
requires requires (vkEnumerateFunc func, Args... args) { {func(args..., 0, 0)} -> VkResult; }
bool get_vk_array(vkEnumerateFunc func, std::vector<vkEnumObj>& result, Args... args)
{
  uint32_t count = 0;
  if (func(args..., &count, nullptr) == VK_SUCCESS)
  {
    result.resize(count);
    if (count > 0)
      return func(args..., &count, result.data()) == VK_SUCCESS;
    return true;
  }
  return false;
}


} // namespace vktools
