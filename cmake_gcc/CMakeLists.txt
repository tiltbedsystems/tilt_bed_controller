# Define minimal required version of CMake.
cmake_minimum_required(VERSION "3.25")

# Project definition
project(
	tilt_bed_controller
	VERSION 1.0
	LANGUAGES C CXX ASM
)

# Include the definition of the slc_tilt_bed_controller target,
# which contains the content of the SLC project
include(tilt_bed_controller.cmake)

add_executable(tilt_bed_controller
    # Add additional sources here
)

target_include_directories(tilt_bed_controller PUBLIC
    # Add additional include paths here
)

target_compile_definitions(tilt_bed_controller PUBLIC
    # Add additional macros here
)

target_compile_options(tilt_bed_controller PUBLIC
    # Set additional compiler flags here
)

target_link_options(tilt_bed_controller PUBLIC
    # Set additional linker flags here
)

# Link with the content defined in the SLC project
target_link_libraries(tilt_bed_controller PRIVATE
    slc
)

# Include managed project content if available
include(tilt_bed_controller_project.cmake OPTIONAL RESULT_VARIABLE managed_project)
if(managed_project)
    message(STATUS "Using managed project content from ${managed_project}")
endif()

# Force the gcc linker command
set_target_properties(tilt_bed_controller PROPERTIES LINKER_LANGUAGE C)

# Create .bin, .hex and .s37 artifacts after building the project
add_custom_command(TARGET tilt_bed_controller
    POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} ${OBJCOPY_SREC_CMD} "$<TARGET_FILE:tilt_bed_controller>" "$<TARGET_FILE_DIR:tilt_bed_controller>/$<TARGET_FILE_BASE_NAME:tilt_bed_controller>.s37"
    COMMAND ${CMAKE_OBJCOPY} ${OBJCOPY_IHEX_CMD} "$<TARGET_FILE:tilt_bed_controller>" "$<TARGET_FILE_DIR:tilt_bed_controller>/$<TARGET_FILE_BASE_NAME:tilt_bed_controller>.hex"
    COMMAND ${CMAKE_OBJCOPY} ${OBJCOPY_BIN_CMD}  "$<TARGET_FILE:tilt_bed_controller>" "$<TARGET_FILE_DIR:tilt_bed_controller>/$<TARGET_FILE_BASE_NAME:tilt_bed_controller>.bin" 
)

# Run post-build pipeline to perform additional post-processing
if(post_build_command)
add_custom_command(TARGET tilt_bed_controller
    POST_BUILD
    WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/..
    COMMAND ${post_build_command}
)
endif()