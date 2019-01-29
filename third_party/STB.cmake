message(STATUS "Populating stb")
FetchContent_Populate(stb
  GIT_REPOSITORY https://github.com/nothings/stb
  GIT_SHALLOW TRUE # stb HEAD "should be" stable
  UPDATE_DISCONNECTED ${THIRD_PARTY_UPDATE_DISCONNECTED}
  SOURCE_DIR ${CMAKE_CURRENT_BINARY_DIR}/stb
  QUIET
)

file(WRITE ${stb_BINARY_DIR}/stb.cc "#define STB_IMAGE_IMPLEMENTATION\n\
#include \"stb_image.h\"")

add_library(stb ${stb_BINARY_DIR}/stb.cc)
target_include_directories(stb PUBLIC ${stb_SOURCE_DIR})
