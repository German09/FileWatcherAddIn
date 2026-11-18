cmake_minimum_required(VERSION 3.16)
project(FileWatcher VERSION 1.3.5 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

add_library(FileWatcher SHARED
    src/dllmain.cpp
    src/FileWatcherComponent.cpp
    src/util/StringConv.cpp
    src/util/JsonEscape.cpp
    src/util/FileText.cpp
    src/watch/EventQueue.cpp
    src/watch/WinWatcher.cpp
    src/watch/LinuxWatcher.cpp
)

target_include_directories(FileWatcher
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(FileWatcher
    PRIVATE
        UNICODE
        _UNICODE
)

if(WIN32)
    target_compile_definitions(FileWatcher PRIVATE _WINDOWS _WIN32)
    target_sources(FileWatcher PRIVATE src/FileWatcher.def)
    target_link_libraries(FileWatcher PRIVATE bcrypt)
    set_target_properties(FileWatcher PROPERTIES
        OUTPUT_NAME "FileWatcher"
        PREFIX ""
    )
    if(MSVC)
        # /utf-8: source has Cyrillic u"..." literals; without it FindMethod(RU) returns -1
        target_compile_options(FileWatcher PRIVATE /W3 /EHsc /utf-8)
        # Self-contained DLL: no vcruntime140.dll / msvcp140.dll needed on client machines
        set_property(TARGET FileWatcher PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    else()
        # Standalone DLL for 1C clients: no libc++.dll / libunwind.dll beside the add-in
        target_link_options(FileWatcher PRIVATE -static)
    endif()
else()
    find_package(Threads REQUIRED)
    target_link_libraries(FileWatcher PRIVATE Threads::Threads)
    find_library(UUID_LIB uuid)
    if(UUID_LIB)
        target_link_libraries(FileWatcher PRIVATE ${UUID_LIB})
    endif()
    set_target_properties(FileWatcher PROPERTIES
        OUTPUT_NAME "FileWatcher"
        PREFIX ""
        SUFFIX ".so"
    )
    # Ensure NativeAPI entry points are exported
    target_compile_options(FileWatcher PRIVATE -Wall -Wextra)
    target_link_options(FileWatcher PRIVATE
        -Wl,--export-dynamic
        -Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/src/exports.map
    )
endif()

# Helpful output layout
set_target_properties(FileWatcher PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

install(TARGETS FileWatcher
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION bin
)
