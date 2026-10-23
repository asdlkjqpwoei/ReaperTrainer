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

set(CMAKE_CONFIGURATION_TYPES "Debug;Release" CACHE STRING "Supported build configurations" FORCE)
list(APPEND CMAKE_VS_GLOBALS
  "VcpkgEnabled=false"
  "MSBuildProjectExtensionsPath=$(MSBuildProjectDirectory)\\$(MSBuildProjectName).dir\\NuGet\\"
)

project(ReaperTrainer LANGUAGES CXX)
include(CTest)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}\\bin\\$<CONFIG>")
set(CMAKE_PDB_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}\\bin\\$<CONFIG>")

add_compile_definitions(UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
add_compile_options(/W4 /sdl /permissive- /utf-8)

set(reaperTrainerRuntimeSources
  "src\\ProcessMemory.cpp"
  "src\\ReaperGameSession.cpp"
  "src\\TrainerConfig.cpp"
)
set(reaperTrainerGeneratedDirectory
  "${CMAKE_CURRENT_BINARY_DIR}\\ReaperTrainer.dir\\$<CONFIG>"
)
set(reaperTrainerGeneratedSources
  "${reaperTrainerGeneratedDirectory}\\XamlTypeInfo.Impl.g.cpp"
  "${reaperTrainerGeneratedDirectory}\\XamlTypeInfo.g.cpp"
  "${reaperTrainerGeneratedDirectory}\\module.g.cpp"
)

add_executable(ReaperTrainer WIN32
  ${reaperTrainerRuntimeSources}
  ${reaperTrainerGeneratedSources}
  "src\\main.cpp"
  "include\\ReaperTrainer\\ProcessMemory.h"
  "include\\ReaperTrainer\\ReaperGameSession.h"
  "include\\ReaperTrainer\\TrainerConfig.h"
  App.idl
  App.xaml
  App.xaml.h
  app.manifest
  "config\\trainer.yaml"
  "cmake\\ReaperTrainer.props"
  "cmake\\ReaperTrainer.targets"
  CMakePresets.json
  NuGet.Config
  README.md
)

foreach(configuration IN LISTS CMAKE_CONFIGURATION_TYPES)
  set_source_files_properties(
    "${CMAKE_CURRENT_BINARY_DIR}\\ReaperTrainer.dir\\${configuration}\\XamlTypeInfo.Impl.g.cpp"
    "${CMAKE_CURRENT_BINARY_DIR}\\ReaperTrainer.dir\\${configuration}\\XamlTypeInfo.g.cpp"
    "${CMAKE_CURRENT_BINARY_DIR}\\ReaperTrainer.dir\\${configuration}\\module.g.cpp"
    PROPERTIES GENERATED TRUE
  )
endforeach()
set_source_files_properties(${reaperTrainerRuntimeSources} PROPERTIES
  VS_SETTINGS "ObjectFileName=$(IntDir)%(Filename).obj"
)
set_source_files_properties(App.xaml PROPERTIES
  VS_TOOL_OVERRIDE ApplicationDefinition
  VS_SETTINGS "Generator=MSBuild:Compile;SubType=Designer;Link=App.xaml"
)

target_include_directories(ReaperTrainer PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}\\include"
  "${CMAKE_CURRENT_SOURCE_DIR}\\src"
  "${reaperTrainerGeneratedDirectory}"
)
target_compile_definitions(ReaperTrainer PRIVATE DISABLE_XAML_GENERATED_MAIN)
target_compile_options(ReaperTrainer PRIVATE /FIunknwn.h)
target_link_libraries(ReaperTrainer PRIVATE
  bcrypt windowsapp
  "$(_FoundationLibFolder)\\Microsoft.WindowsAppRuntime.Bootstrap.lib"
)

file(TO_NATIVE_PATH "${CMAKE_CURRENT_SOURCE_DIR}" reaperTrainerSourceDirectory)
set_target_properties(ReaperTrainer PROPERTIES
  VS_GLOBAL_ROOTNAMESPACE ReaperTrainer
  VS_GLOBAL_ReaperTrainerSourceDirectory "${reaperTrainerSourceDirectory}\\"
  VS_GLOBAL_UseWinUI true
  VS_GLOBAL_CppWinRTPackage true
  VS_GLOBAL_CppWinRTGenerateWindowsMetadata true
  VS_GLOBAL_CppWinRTOptimized true
  VS_GLOBAL_CppWinRTRootNamespaceAutoMerge true
  VS_GLOBAL_WindowsPackageType None
  VS_GLOBAL_AppxPackage false
  VS_GLOBAL_WindowsAppSDKSelfContained false
  VS_GLOBAL_WindowsAppSDKBootstrapInitialize false
  VS_GLOBAL_RestoreConfigFile "${reaperTrainerSourceDirectory}\\NuGet.Config"
  VS_GLOBAL_ForceImportBeforeCppTargets "${reaperTrainerSourceDirectory}\\cmake\\ReaperTrainer.props"
  VS_GLOBAL_ForceImportAfterCppTargets "${reaperTrainerSourceDirectory}\\cmake\\ReaperTrainer.targets"
  VS_PACKAGE_REFERENCES "Microsoft.WindowsAppSDK_1.8.260317003;Microsoft.Windows.CppWinRT_2.0.250303.1"
)

add_custom_command(TARGET ReaperTrainer POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}\\config\\trainer.yaml"
    "$<TARGET_FILE_DIR:ReaperTrainer>\\trainer.yaml"
  VERBATIM
)

if(BUILD_TESTING)
  add_executable(ReaperTrainerTests "test\\ReaperTrainerTests.cpp")
  target_include_directories(ReaperTrainerTests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}\\include"
  )
  target_link_libraries(ReaperTrainerTests PRIVATE
    "${reaperTrainerGeneratedDirectory}\\ReaperTrainerRuntime.lib"
    bcrypt ole32 shell32
  )
  add_dependencies(ReaperTrainerTests ReaperTrainer)
  add_test(
    NAME ReaperTrainerTests
    COMMAND "$<TARGET_FILE:ReaperTrainerTests>"
      "${CMAKE_CURRENT_SOURCE_DIR}\\config\\trainer.yaml"
  )
  set_tests_properties(ReaperTrainerTests PROPERTIES
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  )
endif()

set_property(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  PROPERTY VS_STARTUP_PROJECT ReaperTrainer
)

message(STATUS "Generated projects: ${CMAKE_CURRENT_BINARY_DIR}")
message(STATUS "Binary output: ${CMAKE_CURRENT_SOURCE_DIR}\\bin")
