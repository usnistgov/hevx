FetchContent_Declare(imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui
  GIT_SHALLOW TRUE # imgui "should" be stable at head
)

FetchContent_GetProperties(imgui)
if(NOT imgui_POPULATED)
  message(STATUS "Populating build dependency: imgui")
  FetchContent_Populate(imgui)

  add_library(imgui 
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
  )

  target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR})
endif()
