find_package(Doxygen REQUIRED)

if(BUILD_DOCS_INTERNAL)
  set(INTERNAL_DOCS "YES")
else()
  set(INTERNAL_DOCS "NO")
endif()

configure_file(Doxyfile.in Doxyfile @ONLY)
configure_file(conf.py.in conf.py @ONLY)

add_custom_target(docs ALL
  Doxygen::doxygen Doxyfile
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
  #COMMAND sphinx-build -c "." "${CMAKE_CURRENT_SOURCE_DIR}/sphinx" "rtd"
