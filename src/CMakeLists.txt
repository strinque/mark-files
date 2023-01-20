cmake_minimum_required(VERSION 3.20)
project(mark-files)
set(TARGET_EXE "mark-files-exe")
set(TARGET_NAME "mark-files")

# set required c++ version
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# set project source-files
set(SOURCE_FILES
  mark-files.cpp)
set(HEADER_FILES
  CrossAppMutex.hpp)
set(RESOURCE_FILES
  resource.h
  Resource.rc)

# compile executable
add_executable(${TARGET_EXE} ${SOURCE_FILES} ${HEADER_FILES} ${RESOURCE_FILES})
set_target_properties(${TARGET_EXE} PROPERTIES OUTPUT_NAME ${TARGET_NAME})

# list of required third-party libraries
find_package(fmt CONFIG REQUIRED)
find_package(indicators CONFIG REQUIRED)
find_package(libfort CONFIG REQUIRED)
find_package(console CONFIG REQUIRED)
find_package(files CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)

# set project compile definitions
target_compile_definitions(${TARGET_EXE}
  PRIVATE
    NOMINMAX
    FMT_HEADER_ONLY)

# force utf-8 encoding for source-files
add_compile_options($<$<C_COMPILER_ID:MSVC>:/utf-8>)
add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

# generic configuration
string(CONCAT CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded"
  $<$<CONFIG:Debug>:DebugDLL>
  $<$<NOT:$<CONFIG:Debug>>:DLL>)
string(CONCAT BUILD_PREFIX_PATH
  $<$<CONFIG:Debug>:${PACKAGE_PREFIX_DIR}/debug>
  $<$<NOT:$<CONFIG:Debug>>:${PACKAGE_PREFIX_DIR}>)
target_link_options(${TARGET_EXE}
  PRIVATE
    $<$<NOT:$<CONFIG:Debug>>:/LTCG>
    $<$<NOT:$<CONFIG:Debug>>:/INCREMENTAL:NO>)

# link third-party libraries
target_link_directories(${TARGET_EXE} 
  PRIVATE 
    ${BUILD_PREFIX_PATH}/lib)
target_link_libraries(${TARGET_EXE}
  PRIVATE
    indicators::indicators
    libfort::fort
    fmt::fmt-header-only
    console::console
    files::files
    nlohmann_json::nlohmann_json
    OpenSSL::Crypto)

# compress executable using upx
if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
  if(EXISTS ${CMAKE_INSTALL_PREFIX}/bin/upx.exe)
    add_custom_command(
      TARGET ${TARGET_EXE} POST_BUILD 
      COMMAND ${CMAKE_INSTALL_PREFIX}/bin/upx.exe -9q $<TARGET_FILE:${TARGET_EXE}>)
  endif()
endif()

# install executable
install(TARGETS ${TARGET_EXE} EXPORT ${TARGET_EXE}Config
  RUNTIME DESTINATION ${CMAKE_INSTALL_PREFIX}/bin)

# install executable dependencies
install(CODE [[
    file(GET_RUNTIME_DEPENDENCIES 
        DIRECTORIES ${BUILD_PREFIX_PATH}/bin
        PRE_EXCLUDE_REGEXES "api-ms-*"
        POST_EXCLUDE_REGEXES ".*system32/.*\\.dll"
        RESOLVED_DEPENDENCIES_VAR deps
        EXECUTABLES ${CMAKE_ABSOLUTE_DESTINATION_FILES})
    message(STATUS ${deps})
    file(INSTALL DESTINATION ${CMAKE_INSTALL_PREFIX}/bin TYPE EXECUTABLE FILES ${deps})
]])

# organize files for visual-studio
set_property(GLOBAL PROPERTY USE_FOLDERS ON)
source_group("Headers Files" FILES ${HEADER_FILES})
source_group("Resources Files" FILES ${RESOURCE_FILES})
source_group("Sources Files" FILES ${SOURCE_FILES})