cmake_minimum_required(VERSION 3.24)
project(DOR3 VERSION 1.0 LANGUAGES CXX)
set(CMAKE_CXX_EXTENSIONS OFF)

option(DOR3_BUILD_GUI "Build wxWidgets application" ON)
option(DOR3_BUILD_TESTS "Build core tests" ON)
option(DOR3_ENABLE_SANITIZERS "Enable ASan and UBSan" OFF)

include(FetchContent)

set(TREESETLAB_BUILD_APP OFF CACHE BOOL "Build the LR-1 application" FORCE)
set(TREESETLAB_BUILD_TESTS OFF CACHE BOOL "Build LR-1 tests" FORCE)
set(TREESETLAB_BUILD_BENCHMARK OFF CACHE BOOL "Build the LR-1 benchmark" FORCE)

FetchContent_Declare(
    treesetlab
    GIT_REPOSITORY https://github.com/VenEraBle842/AD3.git
    GIT_TAG 406eff221ae7db82aef6d9ccfec0360203600aaa
    GIT_SHALLOW FALSE
    UPDATE_DISCONNECTED TRUE
)

FetchContent_Declare(
    dor2
    GIT_REPOSITORY https://github.com/VenEraBle842/DOR2.git
    GIT_TAG 54999e9aa9637d924689ea8a09a5db65504f0529
    GIT_SHALLOW FALSE
    UPDATE_DISCONNECTED TRUE
    SOURCE_SUBDIR dor3-headers-only
)

FetchContent_MakeAvailable(treesetlab dor2)

add_library(dor2_headers INTERFACE)
add_library(DOR2::Headers ALIAS dor2_headers)
target_include_directories(dor2_headers INTERFACE "${dor2_SOURCE_DIR}/include")
target_link_libraries(dor2_headers INTERFACE TreeSetLab::Core)

add_library(dor3_core src/generator.cpp src/storage.cpp src/benchmark.cpp src/application.cpp)
target_compile_features(dor3_core PUBLIC cxx_std_23)
target_include_directories(dor3_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(dor3_core PUBLIC DOR2::Headers)
set_target_properties(dor3_core PROPERTIES CXX_EXTENSIONS OFF)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(dor3_core PUBLIC -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
    if(DOR3_ENABLE_SANITIZERS)
        target_compile_options(dor3_core PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(dor3_core PUBLIC -fsanitize=address,undefined)
    endif()
endif()

if(DOR3_BUILD_GUI)
    find_package(wxWidgets 3.2 REQUIRED COMPONENTS core base)
    find_package(Threads REQUIRED)
    add_executable(dor3 src/gui.cpp)
    target_link_libraries(dor3 PRIVATE dor3_core Threads::Threads ${wxWidgets_LIBRARIES})
    target_include_directories(dor3 SYSTEM PRIVATE ${wxWidgets_INCLUDE_DIRS})
    target_compile_definitions(dor3 PRIVATE ${wxWidgets_DEFINITIONS})
    separate_arguments(dor3_wx_flags NATIVE_COMMAND "${wxWidgets_CXX_FLAGS}")
    target_compile_options(dor3 PRIVATE ${dor3_wx_flags})
endif()

if(DOR3_BUILD_TESTS)
    enable_testing()
    find_package(GTest REQUIRED)
    add_executable(dor3_tests tests/test_graph.cpp tests/test_workflow.cpp tests/test_array_sequence.cpp)
    target_link_libraries(dor3_tests PRIVATE dor3_core GTest::gtest_main)
    include(GoogleTest)
    gtest_discover_tests(dor3_tests)
endif()
