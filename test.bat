@echo off
rem Builds and runs the gameplay tests, and builds the terrain map tool.
rem Neither needs a window or a GPU: they use the game's logic files directly.
rem
rem   test.bat                     run the tests
rem   tests\terrain_map.exe 2026   then print a map of any seed
cd /d "%~dp0"

set CORE=src\player.cpp src\raycast.cpp src\savefile.cpp src\world.cpp src\terrain.cpp ^
 src\noise.cpp src\mesher.cpp src\blocks.cpp src\gpu_mesh.cpp src\gl.cpp

echo Compiling tests ...
g++ -std=c++17 -O1 -Wall -Wextra tests\gameplay_tests.cpp %CORE% -o tests\gameplay_tests.exe -lopengl32 -lgdi32
if errorlevel 1 exit /b 1
g++ -std=c++17 -O2 -Wall -Wextra tests\terrain_map.cpp %CORE% -o tests\terrain_map.exe -lopengl32 -lgdi32
if errorlevel 1 exit /b 1

tests\gameplay_tests.exe
