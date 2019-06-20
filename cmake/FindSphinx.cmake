find_program(SPHINX_EXECUTABLE
  NAMES sphinx-build
  HINTS
    $ENV{HOME}/.local/bin
    $ENV{USERPROFILE}/AppData/Roaming/Python/Python37/Scripts
  DOC "Path to sphinx-build executable"
)

include(FindPackageHandleStandardArgs)

find_package_handle_standard_args(Sphinx
  "Failed to find sphinx-build executable"
  SPHINX_EXECUTABLE
)
