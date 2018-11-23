#ifndef HEV_IRIS_RENDERER_DESCRIPTOR_SET_H_
#define HEV_IRIS_RENDERER_DESCRIPTOR_SET_H_

#include "absl/container/inlined_vector.h"
#include "renderer/impl.h"
#include <system_error>

namespace iris::Renderer {

struct DescriptorSets {
  static tl::expected<DescriptorSets, std::system_error>
  Allocate(VkDescriptorPool pool,
           gsl::span<VkDescriptorSetLayoutBinding> bindings,
           std::uint32_t numSets, std::string name = {}) noexcept;

  VkDescriptorSetLayout layout{VK_NULL_HANDLE};
  absl::FixedArray<VkDescriptorSet> sets;

  DescriptorSets(std::size_t count) noexcept : sets(count) {}
  DescriptorSets(DescriptorSets const&) = delete;
  DescriptorSets(DescriptorSets&& other) noexcept;
  DescriptorSets& operator=(DescriptorSets const&) = delete;
  DescriptorSets& operator=(DescriptorSets&& rhs) noexcept;
  ~DescriptorSets() noexcept;

private:
  std::string name;
}; // struct DescriptorSets

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

