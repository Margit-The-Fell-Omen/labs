# Additional clean files
cmake_minimum_required(VERSION 3.16)

if("${CONFIG}" STREQUAL "" OR "${CONFIG}" STREQUAL "Release")
  file(REMOVE_RECURSE
  [[CMakeFiles\power_schemes_autogen.dir\AutogenUsed.txt]]
  [[CMakeFiles\power_schemes_autogen.dir\ParseCache.txt]]
  "power_schemes_autogen"
  )
endif()
