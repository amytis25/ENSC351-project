# Regenerate build/ folder and makefiles:
rm -rf build/         # Wipes temporary build folder
cmake -S . -B build   # Generate makefiles in build

# Build (compile & link) the project
cmake --build build