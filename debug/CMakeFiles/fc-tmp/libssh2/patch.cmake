cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

message(VERBOSE "Executing patch step for libssh2")

block(SCOPE_FOR VARIABLES)

execute_process(
  WORKING_DIRECTORY "/Users/salabdoulaye/Documents/cmput_350/lab/lab6/CMPUT_350-lab-6-exercise/debug/_deps/libssh2-src"
  COMMAND_ERROR_IS_FATAL LAST
  COMMAND  [====[/opt/homebrew/bin/cmake]====] [====[-DLIBSSH2_DIR=/Users/salabdoulaye/Documents/cmput_350/lab/lab6/CMPUT_350-lab-6-exercise/debug/_deps/libssh2-src]====] [====[-DMODULES_DIR=/Users/salabdoulaye/Documents/cmput_350/lab/lab6/CMPUT_350-lab-6-exercise/debug/_deps/sfml-src/src/SFML/Network/../../../cmake/Modules]====] [====[-P]====] [====[/Users/salabdoulaye/Documents/cmput_350/lab/lab6/CMPUT_350-lab-6-exercise/debug/_deps/sfml-src/tools/libssh2/PatchLibssh2.cmake]====]
)

endblock()
