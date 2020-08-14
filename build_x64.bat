cd .\build_x64

REM Now here is used hardcoded path to CMake. TODO: fix it
C:\dev\cmake_3_18\cmake-3.18.1-win64-x64\cmake-3.18.1-win64-x64\bin\CMake.exe ..\ -DCMAKE_TOOLCHAIN_FILE=C:/dev/vcpkg-2020.01/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows -G "Visual Studio 16 2019" -A "x64"

pause