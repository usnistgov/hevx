message(STATUS "Populating build dependency: stb")
FetchContent_Populate(stb
  GIT_REPOSITORY https://github.com/nothings/stb
  GIT_SHALLOW TRUE # stb HEAD "should be" stable
  QUIET
)

file(WRITE ${stb_BINARY_DIR}/stb.cc "#define STB_IMAGE_IMPLEMENTATION\n\
#include \"stb_image.h\"")

add_library(stb ${stb_BINARY_DIR}/stb.cc)
target_include_directories(stb PUBLIC ${stb_SOURCE_DIR})
