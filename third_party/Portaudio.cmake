set(_portaudio_git_tag pa_stable_v190600_20161030)

message(STATUS "Populating Portaudio")
FetchContent_Populate(portaudio
  GIT_REPOSITORY https://git.assembla.com/portaudio.git
  GIT_SHALLOW TRUE GIT_TAG ${_portaudio_git_tag}
  SOURCE_DIR ${CMAKE_CURRENT_BINARY_DIR}/portaudio
  UPDATE_DISCONNECTED ${THIRD_PARTY_UPDATE_DISCONNECTED}
  QUIET
)

add_subdirectory(${portaudio_SOURCE_DIR} ${portaudio_BINARY_DIR})
add_library(portaudio::portaudio_static ALIAS portaudio_static)
