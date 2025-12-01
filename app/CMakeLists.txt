# CMake configuration for app/ executables

file(GLOB APP_SOURCES "src/*.c")

include_directories(${CMAKE_SOURCE_DIR}/app/include)

find_package(CURL REQUIRED)

# door_system executable (hub)
add_executable(door_system src/door_system.c src/http_api.c src/discord_alert.c src/doorMod.c src/door_udp_handler.c)
target_link_libraries(door_system PRIVATE hal CURL::libcurl)

# doorMod CLI executable (door module runner)
add_executable(doorMod_cli src/doorMod_cli.c src/doorMod.c src/door_udp_handler.c)
target_link_libraries(doorMod_cli PRIVATE hal)
