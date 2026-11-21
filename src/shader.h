// shader.h - Compiles GLSL source code into a GPU program.
//
// A "shader program" is a pair of small programs that run on the graphics card:
//   vertex shader   - runs once per vertex, decides WHERE it lands on screen
//   fragment shader - runs once per pixel the triangle covers, decides its COLOUR
#pragma once

#include "gl.h"
#include "mathlib.h"

class Shader {
public:
    Shader() = default;
    ~Shader();

    // A Shader owns a GPU object, so copying it would make two owners that both
    // try to delete it. "= delete" makes the compiler reject any copy.
    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;

    // Compiles and links. On error shows a message box and returns false.
    bool build(const char* name, const char* vertexSource, const char* fragmentSource);
    void use() const { glUseProgram(program_); }

    // Uniforms are the shader's global inputs, set from C++ before drawing.
    void setInt(const char* name, int v) const { glUniform1i(location(name), v); }
    void setFloat(const char* name, float v) const { glUniform1f(location(name), v); }
    void setVec2(const char* name, float x, float y) const { glUniform2f(location(name), x, y); }
    void setVec3(const char* name, const Vec3& v) const { glUniform3f(location(name), v.x, v.y, v.z); }
    void setVec4(const char* name, float x, float y, float z, float w) const {
        glUniform4f(location(name), x, y, z, w);
    }
    void setMat4(const char* name, const Mat4& m) const {
        glUniformMatrix4fv(location(name), 1, GL_FALSE, m.m);  // GL_FALSE: already column-major
    }

private:
    GLint location(const char* name) const { return glGetUniformLocation(program_, name); }

    GLuint program_ = 0;
};
