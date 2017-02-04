project(wasapi-inject-helper)

set(wasapi-inject-helper_HEADERS
	../../../win-capture/inject-library.h
	../../../win-capture/obfuscate.h)

set(wasapi-inject-helper_SOURCES
	../../../win-capture/inject-library.c
	../../../win-capture/obfuscate.c
	../../../win-capture/inject-helper/inject-helper.c)

if(MSVC)
	add_compile_options("$<$<CONFIG:RelWithDebInfo>:/MT>")
endif()

add_executable(wasapi-inject-helper
	${wasapi-inject-helper_SOURCES})

target_link_libraries(wasapi-inject-helper)

if(NOT is-standalone-build)
	if(CMAKE_SIZEOF_VOID_P EQUAL 8)
		set(_output_suffix "64")
	else()
		set(_output_suffix "32")
	endif()

	set_target_properties(wasapi-inject-helper
		PROPERTIES
			OUTPUT_NAME "inject-helper${_output_suffix}")
	
	install_obs_datatarget(wasapi-inject-helper "obs-plugins/win-wasapi-capture")
endif()
