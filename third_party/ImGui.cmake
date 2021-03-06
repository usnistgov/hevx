message(STATUS "Populating imgui")
FetchContent_Populate(imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui
  GIT_SHALLOW TRUE # imgui "should" be stable at head
  SOURCE_DIR ${CMAKE_CURRENT_BINARY_DIR}/imgui
  UPDATE_DISCONNECTED ${THIRD_PARTY_UPDATE_DISCONNECTED}
  QUIET
)

file(REMOVE ${imgui_SOURCE_DIR}/imconfig.h)
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/imconfig.h DESTINATION ${imgui_SOURCE_DIR})
add_library(imgui 
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_demo.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
)

target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR})
target_link_libraries(imgui PUBLIC glm)
