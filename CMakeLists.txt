cmake_minimum_required(VERSION 3.15)
project(BestspeechSAPI VERSION 2.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(STATUS "Building x64 version")
else()
    message(STATUS "Building x86 version")
endif()

include(FetchContent)

FetchContent_Declare(
    openbst
    GIT_REPOSITORY https://github.com/mudb0y/openbst.git
    GIT_TAG master
)
FetchContent_MakeAvailable(openbst)

file(GLOB OPENBST_SOURCES CONFIGURE_DEPENDS
    ${openbst_SOURCE_DIR}/src/*.c
    ${openbst_SOURCE_DIR}/src/data/*.c
)

if(NOT OPENBST_SOURCES)
    message(FATAL_ERROR "openbst sources not found under ${openbst_SOURCE_DIR}")
endif()

add_library(openbst STATIC ${OPENBST_SOURCES})

target_include_directories(openbst PUBLIC
    ${openbst_SOURCE_DIR}/include
)

if(MSVC)
    target_compile_options(openbst PRIVATE /std:c11 /bigobj /W3 /MP)
    set_property(TARGET openbst PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
    set_target_properties(openbst PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
endif()

FetchContent_Declare(
    sonic
    GIT_REPOSITORY https://github.com/waywardgeek/sonic.git
    GIT_TAG master
)
FetchContent_MakeAvailable(sonic)

add_library(sonic STATIC ${sonic_SOURCE_DIR}/sonic.c)

target_include_directories(sonic PUBLIC ${sonic_SOURCE_DIR})

if(MSVC)
    target_compile_options(sonic PRIVATE /W3 /MP)
    set_property(TARGET sonic PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

add_library(BestspeechSAPI SHARED
    src/sapi_main.cpp
    src/com.cpp
    src/engine.cpp
    src/encoding.cpp
    src/registry.cpp
    src/ISpDataKeyImpl.cpp
    src/IEnumSpObjectTokensImpl.cpp
    src/ISpTTSEngineImpl.cpp
    src/voice_token.cpp
    src/bestspeech_sapi.def
)

target_include_directories(BestspeechSAPI PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(BestspeechSAPI PRIVATE
    openbst
    sonic
    ole32
    oleaut32
    advapi32
    shell32
)

target_compile_definitions(BestspeechSAPI PRIVATE
    _CRT_SECURE_NO_WARNINGS
    UNICODE
    _UNICODE
)

if(MSVC)
    target_compile_options(BestspeechSAPI PRIVATE /W3 /MP)
    set_property(TARGET BestspeechSAPI PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

install(TARGETS BestspeechSAPI
    RUNTIME DESTINATION .
    LIBRARY DESTINATION .
)

install(FILES "${CMAKE_BINARY_DIR}/bin/Release/x64/BestspeechSAPI.dll"
    DESTINATION x64
    OPTIONAL
)

set(CPACK_GENERATOR "INNOSETUP")
set(CPACK_PACKAGE_NAME "Bestspeech SAPI")
set(CPACK_PACKAGE_VENDOR "Gozaltech")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Bestspeech Text-to-Speech SAPI5 Engine")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_VERSION_MAJOR "${PROJECT_VERSION_MAJOR}")
set(CPACK_PACKAGE_VERSION_MINOR "${PROJECT_VERSION_MINOR}")
set(CPACK_PACKAGE_VERSION_PATCH "${PROJECT_VERSION_PATCH}")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "Bestspeech")
set(CPACK_PACKAGE_INSTALL_REGISTRY_KEY "BestspeechSAPI")
set(CPACK_PACKAGE_FILE_NAME "BestspeechSAPI_Setup")

set(CPACK_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/installer")
set(CPACK_INNOSETUP_USE_CMAKE_BOOL_FORMAT ON)

set(CPACK_INNOSETUP_SETUP_AppPublisherURL "http://gozaltech.org")
set(CPACK_INNOSETUP_SETUP_DisableWelcomePage "yes")
set(CPACK_INNOSETUP_SETUP_DisableDirPage "yes")
set(CPACK_INNOSETUP_SETUP_DisableReadyPage "yes")
set(CPACK_INNOSETUP_SETUP_DisableProgramGroupPage "yes")

include(CPack)
