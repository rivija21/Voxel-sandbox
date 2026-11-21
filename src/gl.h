// gl.h - Access to OpenGL 3.3 on Windows.
//
// Windows only ships OpenGL 1.1 functions in opengl32.dll. Every newer function
// (shaders, buffers, vertex arrays ...) lives inside the graphics driver, and we
// must ask the driver for its address at run time with wglGetProcAddress().
//
// GL_FUNCTION_LIST below is an "X-macro": one list of (type, name) pairs that we
// expand three times with different meanings of X:
//   gl.h    -> "extern TYPE name;"         (declare the function pointers)
//   gl.cpp  -> "TYPE name = nullptr;"      (define them)
//   gl.cpp  -> "name = (TYPE)lookup(...);" (fill them in)
// So adding a new OpenGL function means adding exactly one line here.
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN  // skip rarely used parts of windows.h (faster compile)
#endif
#ifndef NOMINMAX
#define NOMINMAX             // stop windows.h defining min()/max() macros that break std::min
#endif
#include <windows.h>
#include <GL/gl.h>     // OpenGL 1.1 types and functions (glClear, glViewport, ...)
#include <GL/glext.h>  // constants and function-pointer types for everything newer

#include <string>

#define GL_FUNCTION_LIST(X)                                          \
    X(PFNGLCREATESHADERPROC,            glCreateShader)              \
    X(PFNGLSHADERSOURCEPROC,            glShaderSource)              \
    X(PFNGLCOMPILESHADERPROC,           glCompileShader)             \
    X(PFNGLGETSHADERIVPROC,             glGetShaderiv)               \
    X(PFNGLGETSHADERINFOLOGPROC,        glGetShaderInfoLog)          \
    X(PFNGLDELETESHADERPROC,            glDeleteShader)              \
    X(PFNGLCREATEPROGRAMPROC,           glCreateProgram)             \
    X(PFNGLATTACHSHADERPROC,            glAttachShader)              \
    X(PFNGLLINKPROGRAMPROC,             glLinkProgram)               \
    X(PFNGLGETPROGRAMIVPROC,            glGetProgramiv)              \
    X(PFNGLGETPROGRAMINFOLOGPROC,       glGetProgramInfoLog)         \
    X(PFNGLDELETEPROGRAMPROC,           glDeleteProgram)             \
    X(PFNGLUSEPROGRAMPROC,              glUseProgram)                \
    X(PFNGLGETUNIFORMLOCATIONPROC,      glGetUniformLocation)        \
    X(PFNGLUNIFORM1IPROC,               glUniform1i)                 \
    X(PFNGLUNIFORM1FPROC,               glUniform1f)                 \
    X(PFNGLUNIFORM2FPROC,               glUniform2f)                 \
    X(PFNGLUNIFORM3FPROC,               glUniform3f)                 \
    X(PFNGLUNIFORM4FPROC,               glUniform4f)                 \
    X(PFNGLUNIFORMMATRIX4FVPROC,        glUniformMatrix4fv)          \
    X(PFNGLGENVERTEXARRAYSPROC,         glGenVertexArrays)           \
    X(PFNGLDELETEVERTEXARRAYSPROC,      glDeleteVertexArrays)        \
    X(PFNGLBINDVERTEXARRAYPROC,         glBindVertexArray)           \
    X(PFNGLGENBUFFERSPROC,              glGenBuffers)                \
    X(PFNGLDELETEBUFFERSPROC,           glDeleteBuffers)             \
    X(PFNGLBINDBUFFERPROC,              glBindBuffer)                \
    X(PFNGLBUFFERDATAPROC,              glBufferData)                \
    X(PFNGLBUFFERSUBDATAPROC,           glBufferSubData)             \
    X(PFNGLENABLEVERTEXATTRIBARRAYPROC, glEnableVertexAttribArray)   \
    X(PFNGLVERTEXATTRIBPOINTERPROC,     glVertexAttribPointer)       \
    X(PFNGLDRAWELEMENTSBASEVERTEXPROC,  glDrawElementsBaseVertex)    \
    X(PFNGLACTIVETEXTUREPROC,           glActiveTexture)             \
    X(PFNGLTEXIMAGE3DPROC,              glTexImage3D)                \
    X(PFNGLGENERATEMIPMAPPROC,          glGenerateMipmap)

// Declare one global function pointer per entry, e.g.
//   extern PFNGLCREATESHADERPROC glCreateShader;
#define X(type, name) extern type name;
GL_FUNCTION_LIST(X)
#undef X

// Optional: only present on drivers with OpenGL 4.3 or KHR_debug. May stay null.
extern PFNGLDEBUGMESSAGECALLBACKPROC glDebugMessageCallback;

// Fills in every pointer in GL_FUNCTION_LIST. Needs a current OpenGL context.
// On failure, `missing` lists the functions the driver did not provide.
bool loadOpenGLFunctions(std::string& missing);

// Looks up any OpenGL or WGL extension function by name (nullptr if missing).
void* getOpenGLProc(const char* name);
