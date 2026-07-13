cmake_minimum_required(VERSION 3.16)

project(branch_predictor_simulator VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

function(branchsim_enable_warnings target)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
        target_compile_options(${target} PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wconversion
            -Wshadow
        )
    elseif(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    endif()
endfunction()

add_library(branchsim_core
    src/predictors.cpp
    src/simulator.cpp
    src/statistics.cpp
    src/trace_parser.cpp
)
target_include_directories(branchsim_core PUBLIC include)
branchsim_enable_warnings(branchsim_core)

add_executable(branchsim src/main.cpp)
target_link_libraries(branchsim PRIVATE branchsim_core)
branchsim_enable_warnings(branchsim)

include(CTest)
if(BUILD_TESTING)
    foreach(test_name IN ITEMS predictors trace_parser simulator)
        add_executable(test_${test_name} tests/test_${test_name}.cpp)
        target_link_libraries(test_${test_name} PRIVATE branchsim_core)
        branchsim_enable_warnings(test_${test_name})
        add_test(NAME ${test_name}_tests COMMAND test_${test_name})
    endforeach()

    add_executable(test_cli tests/test_cli.cpp)
    add_dependencies(test_cli branchsim)
    branchsim_enable_warnings(test_cli)
    add_test(
        NAME cli_tests
        COMMAND test_cli $<TARGET_FILE:branchsim> ${CMAKE_CURRENT_SOURCE_DIR}
    )

    add_test(
        NAME sample_gshare_run
        COMMAND branchsim ${CMAKE_CURRENT_SOURCE_DIR}/traces/simple.trace
                --predictor gshare --table-bits 4 --history-bits 3
    )
    add_test(
        NAME comparison_mode
        COMMAND branchsim ${CMAKE_CURRENT_SOURCE_DIR}/traces/simple.trace --compare
    )
endif()
