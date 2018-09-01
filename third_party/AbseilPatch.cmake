file(STRINGS CMakeLists.txt cmakelists NEWLINE_CONSUME)
string(REPLACE "/DNOMINMAX" "/DNOMINMAX /D_SILENCE_ALL_CXX17_DEPRECATION_WARNINGS"
       cmakelists ${cmakelists})
file(REMOVE CMakeLists.txt)
file(WRITE CMakeLists.txt ${cmakelists})