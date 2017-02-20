#pragma once

#include "vulkan_api.h"

namespace v3d 
{
  void  init(const char* app_name, const char* engine_name);
  void  shutdown();
  void  free_resources();

  // events handlers
  void  on_window_create(VkSurfaceKHR surface);
  void  on_window_resize(VkSurfaceKHR surface);
  void  on_device_lost();
  
  void  render();

  vk::Instance&   get_vk();
  vk::Device&     get_device();
}

