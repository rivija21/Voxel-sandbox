@echo off
rem Builds voxel-sandbox.exe with g++ (MSYS2 UCRT64 / MinGW-w64).
rem
rem   build.bat          optimised build, no console window   -> voxel-sandbox.exe
rem   build.bat debug    debug build for gdb, with a console   -> voxel-sandbox-debug.exe
rem                      (run it with --gldebug to print OpenGL errors there)
rem
rem   -std=c++17     C++17 language standard (std::filesystem, structured bindings, ...)
rem   -O2            optimise: world generation and meshing do millions of operations
rem   -g -O0         (debug) keep debug symbols, do not optimise, so gdb can follow the code
rem   -Wall -Wextra  turn on helpful warnings
rem   -mwindows      GUI program: no black console window behind the game
rem   -s             strip symbol names from the .exe (smaller file)
rem   -static        put the C++ runtime inside the .exe, so it runs on PCs without MSYS2
rem   -lopengl32     OpenGL 1.1 functions and wglGetProcAddress (opengl32.dll)
rem   -lgdi32        pixel formats, SwapBuffers and the HUD font (gdi32.dll)
rem   -luser32       windows, messages and input (user32.dll)
cd /d "%~dp0"
where g++ >nul 2>nul || (
    echo g++ was not found. Add C:\msys64\ucrt64\bin to your PATH.
    exit /b 1
)

set SOURCES=src\main.cpp src\window.cpp src\gl.cpp src\shader.cpp src\game.cpp src\world.cpp ^
 src\terrain.cpp src\noise.cpp src\mesher.cpp src\gpu_mesh.cpp src\blocks.cpp src\textures.cpp ^
 src\player.cpp src\raycast.cpp src\renderer.cpp src\hud.cpp src\savefile.cpp

if /i "%~1"=="debug" (
    set FLAGS=-std=c++17 -g -O0 -Wall -Wextra
    set OUT=voxel-sandbox-debug.exe
) else (
    set FLAGS=-std=c++17 -O2 -Wall -Wextra -mwindows -s
    set OUT=voxel-sandbox.exe
)

echo Compiling %OUT% ...
g++ %FLAGS% %SOURCES% -o %OUT% -static -lopengl32 -lgdi32 -luser32
if errorlevel 1 (
    echo Build FAILED.
    exit /b 1
)
echo Built %OUT%
