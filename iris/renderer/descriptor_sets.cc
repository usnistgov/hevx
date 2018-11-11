#include "renderer/descriptor_sets.h"
#include "logging.h"

tl::expected<iris::Renderer::DescriptorSets, std::system_error>
iris::Renderer::DescriptorSets::Create(
  gsl::span<VkDescriptorSetLayoutBinding> bindings, std::uint32_t numSets,
  std::string name) noexcept {
  IRIS_LOG_ENTER();
  Expects(sDevice != VK_NULL_HANDLE);

  DescriptorSets descriptorSet(numSets);

  VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCI = {};
  descriptorSetLayoutCI.sType =
    VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  descriptorSetLayoutCI.bindingCount = static_cast<uint32_t>(bindings.size());
  descriptorSetLayoutCI.pBindings = bindings.data();

  if (auto result = vkCreateDescriptorSetLayout(sDevice, &descriptorSetLayoutCI,
                                                nullptr, &descriptorSet.layout);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(
      make_error_code(result), "Cannot create descriptor set layout"));
  }

  if (!name.empty()) {
    NameObject(VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, descriptorSet.layout,
               name.c_str());
  }

  absl::FixedArray<VkDescriptorSetLayout> descriptorSetLayouts(
    numSets, descriptorSet.layout);

  VkDescriptorSetAllocateInfo descriptorSetAI = {};
  descriptorSetAI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  descriptorSetAI.descriptorPool = sDescriptorPool;
  descriptorSetAI.descriptorSetCount =
    static_cast<uint32_t>(descriptorSetLayouts.size());
  descriptorSetAI.pSetLayouts = descriptorSetLayouts.data();

  if (auto result = vkAllocateDescriptorSets(sDevice, &descriptorSetAI,
                                             descriptorSet.sets.data());
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(make_error_code(result),
                                            "Cannot create descriptor set"));
  }

  descriptorSet.name = std::move(name);

  Ensures(descriptorSet.layout != VK_NULL_HANDLE);
  IRIS_LOG_LEAVE();
  return std::move(descriptorSet);
} // iris::Renderer::DescriptorSets::Create

iris::Renderer::DescriptorSets::DescriptorSets(DescriptorSets&& other) noexcept
  : layout(other.layout)
  , sets(other.sets.size())
  , name(std::move(other.name)) {
  for (std::size_t i = 0; i < sets.size(); ++i) {
    sets[i] = other.sets[i];
  }

  other.layout = VK_NULL_HANDLE;
} // iris::Renderer::DescriptorSets::DescriptorSets

iris::Renderer::DescriptorSets& iris::Renderer::DescriptorSets::
operator=(DescriptorSets&& rhs) noexcept {
  if (this == &rhs) return *this;
  Expects(sets.size() == rhs.sets.size());

  layout = rhs.layout;
  for (std::size_t i = 0; i < sets.size(); ++i) sets[i] = rhs.sets[i];
  name = std::move(rhs.name);

  rhs.layout = VK_NULL_HANDLE;

  return *this;
} // iris::Renderer::DescriptorSets::operator=

iris::Renderer::DescriptorSets::~DescriptorSets() noexcept {
  if (layout == VK_NULL_HANDLE) return;
  IRIS_LOG_ENTER();

  vkDestroyDescriptorSetLayout(sDevice, layout, nullptr);

  IRIS_LOG_LEAVE();
}
