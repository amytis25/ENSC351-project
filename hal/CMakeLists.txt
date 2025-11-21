# CMakeList.txt for HAL (Hardware Abstraction Layer)
#   Build a library (`hal`) which exposes the LED control functionality
#   Use header as: #include "led.h"

# Find all source files in the src directory. Provide an option to include
# Discord webhook support (DiscordAlert.c) so it can be disabled on systems
# that shouldn't build networking code.
option(WITH_DISCORD "Enable Discord webhook alert support" OFF)
option (WITH_TCP "Enable TCP networking support" OFF)

include_directories(hal/include)
file(GLOB MY_SOURCES "src/*.c")

if(NOT WITH_DISCORD)
	# Remove DiscordAlert.c from the source list if user disabled it.
	list(FILTER MY_SOURCES EXCLUDE REGEX ".*/DiscordAlert.c$")
endif()

if(NOT WITH_TCP)
	# Remove TCP related source files from the list if user disabled it.
	list(FILTER MY_SOURCES EXCLUDE REGEX ".*/door_tcp_client.c$")
endif()
find_package(CURL REQUIRED)
IF(CURL_FOUND)
  INCLUDE_DIRECTORIES(${CURL_INCLUDE_DIR})
  SET(requiredlibs ${requiredlibs} ${CURL_LIBRARIES} )
ELSE(CURL_FOUND)
  MESSAGE(FATAL_ERROR "Could not find the CURL library and development files.")
ENDIF(CURL_FOUND)
# Create the HAL static library
add_library(hal STATIC ${MY_SOURCES})

# Make the include directory available to users of this library
target_include_directories(hal PUBLIC include)

# Link required libraries for LED control and timing
target_link_libraries(hal PUBLIC rt)  # For nanosleep and real-time functions

if(WITH_DISCORD)
	message(STATUS "HAL: Building with DiscordAlert support")
else()
	message(STATUS "HAL: Building WITHOUT DiscordAlert support")
endif()
if(WITH_TCP)
    message(STATUS "HAL: Building with TCP networking support")
else()
    message(STATUS "HAL: Building WITHOUT TCP networking support")
endif()

