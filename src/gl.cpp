// gl.cpp - Loads the OpenGL 3.3 functions from the graphics driver.
#include "gl.h"

// Define every function pointer, starting as nullptr:
//   PFNGLCREATESHADERPROC glCreateShader = nullptr;
#define X(type, name) type name = nullptr;
GL_FUNCTION_LIST(X)
#undef X

PFNGLDEBUGMESSAGECALLBACKPROC glDebugMessageCallback = nullptr;

void* getOpenGLProc(const char* name) {
    // We go through void* because casting between unrelated function-pointer
    // types directly makes g++ warn (-Wcast-function-type).
    void* p = reinterpret_cast<void*>(wglGetProcAddress(name));

    // Some drivers return 1, 2, 3 or -1 instead of nullptr to mean "not found".
    const auto bad = reinterpret_cast<intptr_t>(p);
    if (bad == 0 || bad == 1 || bad == 2 || bad == 3 || bad == -1) {
        // OpenGL 1.1 functions are not returned by wglGetProcAddress; they are
        // ordinary exports of opengl32.dll.
        static HMODULE opengl32 = LoadLibraryA("opengl32.dll");
        p = reinterpret_cast<void*>(GetProcAddress(opengl32, name));
    }
    return p;
}

bool loadOpenGLFunctions(std::string& missing) {
    missing.clear();
    // Expands to one line per function, e.g.
    //   glCreateShader = (PFNGLCREATESHADERPROC)getOpenGLProc("glCreateShader");
    //   if (!glCreateShader) missing += "glCreateShader ";
#define X(type, name)                                        \
    name = reinterpret_cast<type>(getOpenGLProc(#name));     \
    if (!name) missing += #name " ";
    GL_FUNCTION_LIST(X)
#undef X

    glDebugMessageCallback =
        reinterpret_cast<PFNGLDEBUGMESSAGECALLBACKPROC>(getOpenGLProc("glDebugMessageCallback"));
    return missing.empty();
}
