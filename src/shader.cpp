// shader.cpp - GLSL compile / link with readable error messages.
#include "shader.h"

#include <string>

// Compiles one stage (vertex or fragment). Returns 0 on failure and fills `log`.
static GLuint compileStage(GLenum stage, const char* source, std::string& log) {
    GLuint shader = glCreateShader(stage);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char buffer[2048];
        glGetShaderInfoLog(shader, sizeof(buffer), nullptr, buffer);
        log = buffer;
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

Shader::~Shader() {
    if (program_) glDeleteProgram(program_);
}

bool Shader::build(const char* name, const char* vertexSource, const char* fragmentSource) {
    std::string log;
    GLuint vs = compileStage(GL_VERTEX_SHADER, vertexSource, log);
    GLuint fs = vs ? compileStage(GL_FRAGMENT_SHADER, fragmentSource, log) : 0;

    if (vs && fs) {
        program_ = glCreateProgram();
        glAttachShader(program_, vs);
        glAttachShader(program_, fs);
        glLinkProgram(program_);

        GLint ok = GL_FALSE;
        glGetProgramiv(program_, GL_LINK_STATUS, &ok);
        if (!ok) {
            char buffer[2048];
            glGetProgramInfoLog(program_, sizeof(buffer), nullptr, buffer);
            log = buffer;
            glDeleteProgram(program_);
            program_ = 0;
        }
    }
    // Once linked, the program keeps its own copy; the stage objects can go.
    if (vs) glDeleteShader(vs);
    if (fs) glDeleteShader(fs);

    if (!program_) {
        std::string message = std::string("Shader \"") + name + "\" failed to build:\n\n" + log;
        MessageBoxA(nullptr, message.c_str(), "Voxel Sandbox - shader error", MB_ICONERROR);
        return false;
    }
    return true;
}
