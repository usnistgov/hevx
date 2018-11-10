#include "renderer/descriptor_set.h"
#include "logging.h"

tl::expected<iris::Renderer::DescriptorSet, std::system_error>
iris::Renderer::DescriptorSet::Create(
  gsl::span<VkDescriptorSetLayoutBinding> bindings, std::string name) noexcept {
  IRIS_LOG_ENTER();
  DescriptorSet descriptorSet;
  descriptorSet.sets.resize(bindings.size());

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
    bindings.size(), descriptorSet.layout);

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

  IRIS_LOG_LEAVE();
  return std::move(descriptorSet);
} // iris::Renderer::DescriptorSet::Create

iris::Renderer::DescriptorSet::DescriptorSet(DescriptorSet&& other) noexcept
  : layout(other.layout)
  , sets(std::move(other.sets))
  , name(std::move(other.name)) {
  other.layout = VK_NULL_HANDLE;
} // iris::Renderer::DescriptorSet::DescriptorSet

iris::Renderer::DescriptorSet& iris::Renderer::DescriptorSet::
operator=(DescriptorSet&& rhs) noexcept {
  if (this == &rhs) return *this;

  layout = rhs.layout;
  sets = std::move(rhs.sets);
  name = std::move(rhs.name);

  rhs.layout = VK_NULL_HANDLE;

  return *this;
} // iris::Renderer::DescriptorSet::operator=

iris::Renderer::DescriptorSet::~DescriptorSet() noexcept {
  if (layout == VK_NULL_HANDLE) return;
  IRIS_LOG_ENTER();

  vkDestroyDescriptorSetLayout(sDevice, layout, nullptr);

  IRIS_LOG_LEAVE();
}
