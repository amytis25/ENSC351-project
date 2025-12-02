rm -rf build/

# Set cross-compilation environment
export CC=aarch64-linux-gnu-gcc
export CXX=aarch64-linux-gnu-g++
export PKG_CONFIG_PATH=/usr/lib/aarch64-linux-gnu/pkgconfig
export PKG_CONFIG_LIBDIR=/usr/lib/aarch64-linux-gnu/pkgconfig

# Run CMake with explicit paths for ARM libraries
cmake -S . -B build \
    -DCURL_LIBRARY=/usr/lib/aarch64-linux-gnu/libcurl.so \
    -DCURL_INCLUDE_DIR=/usr/include/aarch64-linux-gnu

cmake --build build