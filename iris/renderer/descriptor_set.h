#ifndef HEV_IRIS_RENDERER_DESCRIPTOR_SET_H_
#define HEV_IRIS_RENDERER_DESCRIPTOR_SET_H_

#include "renderer/impl.h"
#include "tl/expected.hpp"
#include <system_error>
#include <vector>

namespace iris::Renderer {

struct DescriptorSet {
  static tl::expected<DescriptorSet, std::system_error>
  Create(gsl::span<VkDescriptorSetLayoutBinding> bindings,
         std::string name = {}) noexcept;

  VkDescriptorSetLayout layout;
  std::vector<VkDescriptorSet> sets;

  DescriptorSet() = default;
  DescriptorSet(DescriptorSet const&) = delete;
  DescriptorSet(DescriptorSet&& other) noexcept;
  DescriptorSet& operator=(DescriptorSet const&) = delete;
  DescriptorSet& operator=(DescriptorSet&& rhs) noexcept;
  ~DescriptorSet() noexcept;

private:
  std::string name;
}; // struct DescriptorSet

inline void UpdateDescriptorSets(
  gsl::span<VkWriteDescriptorSet> writeDescriptorSets,
  gsl::span<VkCopyDescriptorSet> copyDescriptorSets = {}) noexcept {
  vkUpdateDescriptorSets(sDevice,
                         static_cast<uint32_t>(writeDescriptorSets.size()),
                         writeDescriptorSets.data(),
                         static_cast<uint32_t>(copyDescriptorSets.size()),
                         copyDescriptorSets.data());
} // UpdateDescriptorSets

} // namespace iris::Renderer

#endif // HEV_IRIS_RENDERER_DESCRIPTOR_SET_H_

