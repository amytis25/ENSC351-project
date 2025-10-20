# Build the reaction timer application, using the HAL

# Find all source files in current directory
include_directories(include)
file(GLOB MY_SOURCES "*.c")

# Create the doorMod executable
add_executable(doorMod ${MY_SOURCES})

# Link with the HAL library
target_link_libraries(doorMod LINK_PRIVATE hal)

# Link additional libraries needed for timing and math functions
target_link_libraries(doorMod LINK_PRIVATE rt m)

# Copy executable to final location for deployment
add_custom_command(TARGET doorMod POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E copy
     "$<TARGET_FILE:doorMod>"
     "~/ensc351Proj/public/doorMod"
  COMMENT "Copying ARM executable to public NFS directory")


