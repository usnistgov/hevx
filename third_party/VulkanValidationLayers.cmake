set(_vulkanvalidationlayers_git_git sdk-1.1.97.0)

message(STATUS "Populating build dependency: VulkanValidationLayers")
FetchContent_Populate(VulkanValidationLayers
  GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-ValidationLayers
  GIT_SHALLOW TRUE GIT_TAG ${_vulkanvalidationlayers_git_git}
  QUIET
)

set(GLSLANG_INSTALL_DIR ${GLSLANG_SOURCE_DIR})
set(GLSLANG_SPIRV_INCLUDE_DIR ${GLSLANG_SOURCE_DIR})
set(SPIRV_TOOLS_BINARY_ROOT ${GLSLANG_BINARY_DIR}/External/spirv-tools/tools)
set(SPIRV_TOOLS_OPT_BINARY_ROOT ${SPIRV_TOOLS_BINARY_ROOT})
set(SPIRV_TOOLS_INCLUDE_DIR ${GLSLANG_SOURCE_DIR}/External/spirv-tools/include)

if(UNIX)
  set(GLSLANG_LIB ${GLSLANG_BINARY_DIR}/glslang/libglslang.a)
  set(OGLCompiler_LIB ${GLSLANG_BINARY_DIR}/OGLCompilersDLL/libOGLCompiler.a)
  set(OSDependent_LIB
    ${GLSLANG_BINARY_DIR}/glslang/OSDependent/Unix/libOSDependent.a)
  set(HLSL_LIB ${GLSLANG_BINARY_DIR}/hlsl/libHLSL.a)
  set(SPIRV_LIB ${GLSLANG_BINARY_DIR}/SPIRV/libSPIRV.a)
  set(SPIRV_REMAPPER_LIB ${GLSLANG_BINARY_DIR}/SPIRV/libSPVRemapper.a)

  set(SPIRV_TOOLS_LIB
    ${GLSLANG_BINARY_DIR}/External/spirv-tools/source/libSPIRV-Tools.a)
  set(SPIRV_TOOLS_OPT_LIB
    ${GLSLANG_BINARY_DIR}/External/spirv-tools/source/opt/libSPIRV-Tools-opt.a)
endif()

add_subdirectory(${vulkanvalidationlayers_SOURCE_DIR}
  ${vulkanvalidationlayers_BINARY_DIR}
)
