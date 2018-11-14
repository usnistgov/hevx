#ifndef HEV_IRIS_RENDERER_MODEL_H_
#define HEV_IRIS_RENDERER_MODEL_H_

#include "iris/renderer/impl.h"
#include "iris/renderer/buffer.h"
#include "iris/renderer/pipeline.h"
#include "gsl/gsl"
#include <cstdint>
#include <string>
#include <vector>

namespace iris::Renderer {

struct BufferDescription {
  VkBufferUsageFlagBits bufferUsage;
  VmaMemoryUsage memoryUsage;
  std::uint32_t size;
  std::vector<std::byte> data;

  BufferDescription(VkBufferUsageFlagBits b, VmaMemoryUsage m, std::uint32_t s)
    : bufferUsage(b)
    , memoryUsage(m)
    , size(s) {}

  BufferDescription(VkBufferUsageFlagBits b, VmaMemoryUsage m,
                    gsl::span<std::byte> d)
    : bufferUsage(b)
    , memoryUsage(m)
    , size(gsl::narrow_cast<std::uint32_t>(d.size()))
    , data(d.data(), d.data() + d.size()) {}
}; // struct BufferDescription

struct TextureDescription {
  VkImageType imageType;
  VkFormat format;
  VkExtent3D extent;
  VkImageUsageFlags imageUsage;
  VmaMemoryUsage memoryUsage;
  VkFilter magFilter;
  VkFilter minFilter;
  VkSamplerMipmapMode mipmapMode;
  float minLod;
  float maxLod;
  VkSamplerAddressMode addressModeU;
  VkSamplerAddressMode addressModeV;
  VkSamplerAddressMode addressModeW;

  std::vector<std::byte> data;

  TextureDescription(VkImageType typ, VkFormat fmt, VkExtent3D ext,
                     VkImageUsageFlags usg, VmaMemoryUsage mus, VkFilter mag,
                     VkFilter min, VkSamplerMipmapMode mip, float mlod,
                     float xlod, VkSamplerAddressMode u, VkSamplerAddressMode v,
                     VkSamplerAddressMode w, gsl::span<std::byte> d)
    : imageType(typ)
    , format(fmt)
    , extent(ext)
    , imageUsage(usg)
    , memoryUsage(mus)
    , magFilter(mag)
    , minFilter(min)
    , mipmapMode(mip)
    , minLod(mlod)
    , maxLod(xlod)
    , addressModeU(u)
    , addressModeV(v)
    , addressModeW(w)
    , data(d.data(), d.data() + d.size()) {}
}; // struct TextureDescription

struct ShaderDescription {
  VkShaderStageFlagBits stage;
  std::string entry;
  std::vector<std::uint32_t> code;

  ShaderDescription(VkShaderStageFlagBits s, std::string e,
                    std::vector<std::uint32_t> c)
    : stage(s)
    , entry(std::move(e))
    , code(std::move(c)) {}
}; // struct ShaderDescription

struct MaterialDescription {
}; // struct MaterialDescription

} // namespace iris::Renderer

#endif // HEV_IRIS_RENDERER_MODEL_H_
