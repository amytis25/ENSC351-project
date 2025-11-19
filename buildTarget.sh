sudo beagle-pwm-export --pin GPIO15
rm -rf build/
CC=aarch64-linux-gnu-gcc CXX=aarch64-linux-gnu-g++ cmake -S . -B build
cmake --build build