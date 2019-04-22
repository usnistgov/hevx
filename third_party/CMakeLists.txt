include(FetchContent)

# The following packages must be included first and in this order
include(GoogleTest.cmake)
include(GLM.cmake)
include(Vulkan.cmake)

# These packages can be included in any order
include(Abseil.cmake)
include(Expected.cmake)
include(Flags.cmake)
include(FlextGL.cmake)
include(Fmtlib.cmake)
include(Glslang.cmake)
include(GSL.cmake)
include(ImGui.cmake)
include(JSON.cmake)
include(Protobuf.cmake)
include(Spdlog.cmake)
include(STB.cmake)
include(TBB.cmake)
include(VulkanMemoryAllocator.cmake)
include(Websocketpp.cmake)

# These packages must be included last
include(Cpprestsdk.cmake)

add_subdirectory(mikktspace)
