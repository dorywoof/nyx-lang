add_executable(nyx_unit_tests
    test_main.c
    test_scanner.c
    test_chunk.c
    test_table.c
    test_vm.c
)
target_link_libraries(nyx_unit_tests PRIVATE nyxcore)
target_include_directories(nyx_unit_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_test(NAME unit COMMAND nyx_unit_tests)

find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_FOUND)
    add_test(
        NAME golden
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/run_golden.py
                $<TARGET_FILE:nyx> ${CMAKE_CURRENT_SOURCE_DIR}/lang
    )
else()
    message(STATUS "Python3 not found -- skipping golden-file language tests (unit tests still run)")
endif()
