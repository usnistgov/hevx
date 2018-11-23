file(STRINGS CMakeLists.txt cmakelists NEWLINE_CONSUME)
string(REPLACE "add_executable(tests \${TEST_SOURCES})"
  "add_executable(tests \${TEST_SOURCES})\nadd_test(expected_test tests)"
  cmakelists ${cmakelists})
file(REMOVE CMakeLists.txt)
file(WRITE CMakeLists.txt ${cmakelists})
