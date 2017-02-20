#include "shaders.h"
#include <fstream>

namespace v3d 
{

static std::vector<char> read_file(const std::string& filename) 
{
  std::ifstream file(filename, std::ios::ate | std::ios::binary);
  if (!file.is_open())
    throw std::runtime_error("failed to open file");
  size_t fileSize = (size_t) file.tellg();

  std::vector<char> buffer(fileSize);
  file.seekg(0); 
  file.read(buffer.data(), fileSize);
  return buffer;
}

static vk::ShaderModule  create_shader_module(std::vector<char> const& code)
{
  return v3d::get_device().createShaderModule(
        vk::ShaderModuleCreateInfo()
          .setCodeSize(code.size())
          .setPCode((const uint32_t*)code.data())
      );
}

vk::ShaderModule  load_shader_from_file(const char* filename)
{
  return create_shader_module(read_file(filename));
}

}

