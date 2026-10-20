cmake_minimum_required(VERSION 4.2)

if(NOT WIN32)
  message(FATAL_ERROR "Reaper Trainer requires Windows.")
endif()

if(NOT CMAKE_GENERATOR STREQUAL "Visual Studio 18 2026")
  message(FATAL_ERROR
    "Use the Visual Studio 2026 generator with `cmake --preset vs2026`.")
endif()

if(NOT CMAKE_GENERATOR_PLATFORM STREQUAL "x64")
  message(FATAL_ERROR "Reaper Trainer must be configured for x64.")
endif()

project(ReaperTrainer LANGUAGES CXX)
include(CTest)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

find_program(REAPERTRAINER_MSBUILD_EXECUTABLE
  NAMES MSBuild.exe
  HINTS
    "${CMAKE_GENERATOR_INSTANCE}\\MSBuild\\Current\\Bin"
    "$ENV{VSINSTALLDIR}\\MSBuild\\Current\\Bin"
)

if(NOT REAPERTRAINER_MSBUILD_EXECUTABLE)
  message(FATAL_ERROR
    "MSBuild.exe was not found in the Visual Studio 2026 installation.")
endif()

set(REAPERTRAINER_MODULE_SOURCES
  "src\\ProcessMemory.cpp"
  "src\\ReaperGameSession.cpp"
  "src\\TrainerConfig.cpp"
)

if(BUILD_TESTING)
  add_executable(ReaperTrainerTests
    "test\\ReaperTrainerTests.cpp"
    ${REAPERTRAINER_MODULE_SOURCES}
  )
  target_include_directories(ReaperTrainerTests
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}\\include"
  )
  target_compile_definitions(ReaperTrainerTests
    PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
  )
  target_compile_options(ReaperTrainerTests PRIVATE /W4 /permissive- /utf-8)
  target_link_libraries(ReaperTrainerTests PRIVATE Bcrypt Ole32 Shell32)
  add_test(
    NAME ReaperTrainerTests
    COMMAND ReaperTrainerTests "${CMAKE_CURRENT_SOURCE_DIR}\\config\\trainer.yaml"
  )
  set_tests_properties(ReaperTrainerTests PROPERTIES
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  )
endif()

add_custom_target(ReaperTrainer ALL
  COMMAND "${REAPERTRAINER_MSBUILD_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}\\ReaperTrainer.vcxproj"
    /nologo
    /m
    /restore
    /t:Build
    "/p:Configuration=$<CONFIG>"
    "/p:Platform=x64"
    "/p:OutDir=${CMAKE_BINARY_DIR}\\app\\$<CONFIG>\\"
    "/p:IntDir=${CMAKE_BINARY_DIR}\\intermediate\\$<CONFIG>\\"
  VERBATIM
)
set_property(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  PROPERTY VS_STARTUP_PROJECT ReaperTrainer
)

message(STATUS "MSBuild: ${REAPERTRAINER_MSBUILD_EXECUTABLE}")
message(STATUS "CMake output: ${CMAKE_BINARY_DIR}")
