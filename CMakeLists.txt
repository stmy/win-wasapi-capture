if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
	set(is-standalone-build true)
else()
    set(is-standalone-build false)
endif()

add_subdirectory(source)
