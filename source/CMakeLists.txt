project(win-wasapi-capture)

if(is-standalone-build)
	find_package(libobs REQUIRED)
endif()

include_directories(
	../../../libobs/
	../../../deps/ipc-util)

set(win-wasapi-capture_HEADERS
	../../win-capture/obfuscate.h
	../../win-capture/inject-library.h
	../../win-capture/window-helpers.h
	../../win-capture/hook-helpers.h
	win-wasapi-capture.h
	wasapi-hook-info.h)

set(win-wasapi-capture_SOURCES
	../../win-capture/obfuscate.c
	../../win-capture/inject-library.c
	../../win-capture/window-helpers.c
	win-wasapi-capture.cpp
	plugin-main.cpp)

add_library(win-wasapi-capture MODULE
	${win-wasapi-capture_HEADERS}
	${win-wasapi-capture_SOURCES})

target_link_libraries(win-wasapi-capture
	libobs
	psapi)

if(NOT is-standalone-build)
	install_obs_plugin_with_data(win-wasapi-capture data)
endif()

add_subdirectory(wasapi-hook)
add_subdirectory(inject-helper)
