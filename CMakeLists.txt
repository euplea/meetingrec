cmake_minimum_required(VERSION 3.16)
project(meetingrec LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(meetingrec
    src/main.cpp
    src/minutes.cpp
    src/recorder.cpp
    src/transcriber.cpp
    src/tui.cpp
    src/wav_writer.cpp
)

target_include_directories(meetingrec PRIVATE src)

if(WIN32)
    target_compile_definitions(meetingrec PRIVATE
        _CRT_SECURE_NO_WARNINGS
        NOMINMAX
        WIN32_LEAN_AND_MEAN
        UNICODE
        _UNICODE
    )
    # WASAPI/COM + WinHTTP nativi (nessuna dipendenza esterna).
    target_link_libraries(meetingrec PRIVATE ole32 winhttp)
    # Icona + manifest.
    target_sources(meetingrec PRIVATE resources/app.rc)
    if(MSVC)
        # Il manifest è già incluso nella risorsa .rc.
        target_link_options(meetingrec PRIVATE "/MANIFEST:NO")
    endif()
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(PORTAUDIO REQUIRED IMPORTED_TARGET portaudio-2.0)
    find_package(CURL REQUIRED)
    target_link_libraries(meetingrec PRIVATE PkgConfig::PORTAUDIO CURL::libcurl)
endif()
