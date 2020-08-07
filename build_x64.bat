cd .\build_x64

cmake ..\ -DCMAKE_TOOLCHAIN_FILE=C:/dev/vcpkg-2020.01/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows -G "Visual Studio 15 2017 Win64"

pause