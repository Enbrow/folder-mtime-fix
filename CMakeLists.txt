cmake_minimum_required(VERSION 3.20)
project(FolderMTimeFix VERSION 1.0.0 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Produce a standalone EXE on MSVC without requiring the VC++ runtime package.
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

add_library(mtime_core STATIC
    src/mtime_core.cpp
    src/mtime_core.h
)

target_include_directories(mtime_core PUBLIC src)
target_compile_definitions(mtime_core PUBLIC UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)

add_executable(FolderMTimeFix WIN32
    src/main.cpp
    resources/app.rc
)

target_link_libraries(FolderMTimeFix PRIVATE
    mtime_core
    comctl32
    comdlg32
    shell32
    ole32
    uuid
)

target_compile_definitions(FolderMTimeFix PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)

if(MSVC)
    target_compile_options(mtime_core PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(FolderMTimeFix PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_link_options(FolderMTimeFix PRIVATE
        /MANIFEST:EMBED
        "/MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/resources/app.manifest"
    )
endif()

enable_testing()

add_executable(FolderMTimeFixTests
    tests/test_core.cpp
)
target_link_libraries(FolderMTimeFixTests PRIVATE mtime_core)
target_compile_definitions(FolderMTimeFixTests PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)

if(MSVC)
    target_compile_options(FolderMTimeFixTests PRIVATE /W4 /permissive- /utf-8 /EHsc)
endif()

add_test(NAME FolderMTimeFixTests COMMAND FolderMTimeFixTests)

add_executable(FolderMTimeFixTreeGridTests
    tests/test_tree_grid_logic.cpp
)
target_link_libraries(FolderMTimeFixTreeGridTests PRIVATE mtime_core)
target_compile_definitions(FolderMTimeFixTreeGridTests PRIVATE
    UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)

if(MSVC)
    target_compile_options(FolderMTimeFixTreeGridTests PRIVATE
        /W4 /permissive- /utf-8 /EHsc)
endif()

add_test(NAME FolderMTimeFixTreeGridTests COMMAND FolderMTimeFixTreeGridTests)
