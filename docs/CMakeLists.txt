find_package(Doxygen)

if(BUILD_DOCS_INTERNAL)
  set(INTERNAL_DOCS "YES")
else()
  set(INTERNAL_DOCS "NO")
endif()

if(Doxygen_FOUND)
  find_package(Sphinx REQUIRED)
  configure_file(Doxyfile.in Doxyfile @ONLY)
  configure_file(conf.py.in conf.py @ONLY)

  add_custom_target(docs
    Doxygen::doxygen Doxyfile
    COMMAND ${SPHINX_EXECUTABLE} -c "." "${CMAKE_CURRENT_SOURCE_DIR}" "rtd"
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
  )
endif()
