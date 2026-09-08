# Additional clean files
cmake_minimum_required(VERSION 3.16)

if("${CONFIG}" STREQUAL "" OR "${CONFIG}" STREQUAL "Release")
  file(REMOVE_RECURSE
  "CMakeFiles\\battery_diagnostics_autogen.dir\\AutogenUsed.txt"
  "CMakeFiles\\battery_diagnostics_autogen.dir\\ParseCache.txt"
  "battery_diagnostics_autogen"
  )
endif()
