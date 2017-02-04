project(wasapi-hook)

include_directories(
    ../../../../libobs/
    ../../../../deps/ipc-util)

set(wasapi-hook_HEADERS
    ../../../win-capture/obfuscate.h
    ../../../win-capture/hook-helpers.h
    ../../../win-capture/funchook.h
	../wasapi-hook-info.h)

set(wasapi-hook_SOURCES
    ../../../win-capture/obfuscate.c
    ../../../win-capture/funchook.c
    wasapi-hook.cpp)

add_library(wasapi-hook MODULE
    ${wasapi-hook_HEADERS}
    ${wasapi-hook_SOURCES})

target_link_libraries(wasapi-hook
	ipc-util
    psapi)

if(NOT is-standalone-build)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(_output_suffix "64")
    else()
    	set(_output_suffix "32")
    endif()

    set_target_properties(wasapi-hook
    	PROPERTIES
    		OUTPUT_NAME "wasapi-hook${_output_suffix}")

    install_obs_datatarget(wasapi-hook "obs-plugins/win-wasapi-capture")
endif()
