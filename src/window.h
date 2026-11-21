// window.h - A Win32 window with an OpenGL 3.3 context, keyboard and mouse input.
//
// This is the only part of the game that talks to Windows directly. Everything
// else just asks this class "which keys are down?" and "how big is the window?".
#pragma once

#include "gl.h"  // includes <windows.h>

// Input collected from Windows messages. Key arrays are indexed by Windows
// "virtual-key codes": 'A'..'Z' and '0'..'9' are their ASCII values, and
// others have names such as VK_SPACE, VK_ESCAPE, VK_F1.
struct InputState {
    bool down[256] = {};        // held right now
    bool pressed[256] = {};     // went down during this frame (true for one frame only)
    bool mouseDown[3] = {};     // 0 = left, 1 = right, 2 = middle
    bool mousePressed[3] = {};
    float mouseDX = 0.0f;       // raw mouse movement this frame (only while captured)
    float mouseDY = 0.0f;
    int wheel = 0;              // wheel notches this frame, + = away from you
};

class Window {
public:
    Window() = default;
    ~Window();
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    // Creates the window and an OpenGL 3.3 core context. `debugContext` asks the
    // driver to report mistakes in our OpenGL calls (slower; for development).
    bool create(const char* title, int width, int height, bool debugContext);

    // Handles all waiting Windows messages. Returns false once the window is closed.
    bool processMessages();
    void swapBuffers() { SwapBuffers(hdc_); }  // show the frame we just drew

    double time() const;  // seconds since create(), very precise
    int width() const { return width_; }
    int height() const { return height_; }
    float uiScale() const { return uiScale_; }
    bool hasFocus() const { return focused_; }
    const InputState& input() const { return input_; }

    // While captured the cursor is hidden and locked, and mouse movement turns the camera.
    void setMouseCaptured(bool captured);
    bool mouseCaptured() const { return captured_; }

    void toggleFullscreen();

private:
    // Windows calls this static function for every message. It finds the
    // Window object (stored in the window's "user data") and forwards to handleMessage.
    static LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam);

    bool createContext(bool debugContext);
    void lockCursor();
    void setMouseButton(int button, bool down);
    void clearInput();

    HWND hwnd_ = nullptr;
    HDC hdc_ = nullptr;       // "device context": what we draw into
    HGLRC context_ = nullptr; // the OpenGL rendering context
    int width_ = 0, height_ = 0;
    float uiScale_ = 1.0f;
    bool closed_ = false;
    bool focused_ = false;    // only true while Windows sends our keys to us
    bool captured_ = false;
    bool fullscreen_ = false;
    WINDOWPLACEMENT savedPlacement_{};  // where the window was before going fullscreen
    int wheelRemainder_ = 0;
    InputState input_;
    LARGE_INTEGER timerFrequency_{}, timerStart_{};
};
