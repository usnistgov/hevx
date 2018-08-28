find_package(Doxygen REQUIRED)

if(BUILD_DOCS_INTERNAL)
  set(INTERNAL_DOCS "YES")
else()
  set(INTERNAL_DOCS "NO")
endif()

configure_file(Doxyfile.in Doxyfile @ONLY)

add_custom_target(docs ALL
  Doxygen::doxygen ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile
  WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
)
