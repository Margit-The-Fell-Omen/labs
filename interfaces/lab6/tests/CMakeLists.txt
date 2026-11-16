find_package(Qt6 REQUIRED COMPONENTS Test)

function(add_lab6_test name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE lab6core Qt6::Test)
    target_compile_options(${name} PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

add_lab6_test(test_crc32 test_crc32.cpp)
add_lab6_test(test_frame test_frame.cpp)
add_lab6_test(test_ratemeter test_ratemeter.cpp)
add_lab6_test(test_transfer test_transfer.cpp)

add_executable(test_dbusmeta test_dbusmeta.cpp ../src/ble/dbusutil.cpp)
target_include_directories(test_dbusmeta PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_dbusmeta PRIVATE Qt6::DBus Qt6::Test)
target_compile_options(test_dbusmeta PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME test_dbusmeta COMMAND test_dbusmeta)
