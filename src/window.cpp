// window.cpp - Win32 window, WGL context creation and input handling.
#include "window.h"

#include <GL/wglext.h>  // WGL_CONTEXT_* constants and wglCreateContextAttribsARB's type

#include <algorithm>
#include <cstdio>
#include <iterator>
#include <string>

namespace {

const char* WINDOW_CLASS = "VoxelSandboxWindow";

void showError(const std::string& message) {
    MessageBoxA(nullptr, message.c_str(), "Voxel Sandbox", MB_ICONERROR);
}

// Called by the driver when an OpenGL call goes wrong (debug contexts only).
void APIENTRY openglDebugCallback(GLenum, GLenum type, GLuint, GLenum severity, GLsizei, const GLchar* message,
                                  const void*) {
    if (severity == GL_DEBUG_SEVERITY_NOTIFICATION) return;  // chatter, not problems
    std::fprintf(stderr, "[OpenGL %s] %s\n", type == GL_DEBUG_TYPE_ERROR ? "ERROR" : "warning", message);
    OutputDebugStringA(message);
}

}  // namespace

Window::~Window() {
    ClipCursor(nullptr);
    if (context_) {
        wglMakeCurrent(nullptr, nullptr);
        wglDeleteContext(context_);
    }
    if (hwnd_) {
        if (hdc_) ReleaseDC(hwnd_, hdc_);
        DestroyWindow(hwnd_);
    }
    UnregisterClassA(WINDOW_CLASS, GetModuleHandleA(nullptr));
}

bool Window::create(const char* title, int width, int height, bool debugContext) {
    // Without this, Windows pretends our window is 96 DPI and stretches it,
    // which makes everything blurry on high-DPI laptop screens.
    SetProcessDPIAware();
    HDC screen = GetDC(nullptr);
    uiScale_ = static_cast<float>(GetDeviceCaps(screen, LOGPIXELSY)) / 96.0f;
    ReleaseDC(nullptr, screen);

    HINSTANCE instance = GetModuleHandleA(nullptr);
    WNDCLASSEXA wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;  // CS_OWNDC: OpenGL needs a DC that stays the same
    wc.lpfnWndProc = &Window::windowProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorA(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconA(nullptr, IDI_APPLICATION);
    wc.lpszClassName = WINDOW_CLASS;
    if (!RegisterClassExA(&wc)) {
        showError("RegisterClassEx failed.");
        return false;
    }

    // We want the CLIENT area (inside the borders) to be width x height,
    // scaled for the display, but never bigger than the screen.
    RECT work{};
    SystemParametersInfoA(SPI_GETWORKAREA, 0, &work, 0);
    const int workW = work.right - work.left, workH = work.bottom - work.top;
    const DWORD style = WS_OVERLAPPEDWINDOW;
    RECT rect{0, 0, static_cast<LONG>(width * uiScale_), static_cast<LONG>(height * uiScale_)};
    AdjustWindowRect(&rect, style, FALSE);
    int outerW = rect.right - rect.left, outerH = rect.bottom - rect.top;
    if (outerW > workW) outerW = workW;
    if (outerH > workH) outerH = workH;

    // The last argument (`this`) arrives in WM_NCCREATE, see windowProc.
    hwnd_ = CreateWindowExA(0, WINDOW_CLASS, title, style, work.left + (workW - outerW) / 2,
                            work.top + (workH - outerH) / 2, outerW, outerH, nullptr, nullptr, instance, this);
    if (!hwnd_) {
        showError("CreateWindowEx failed.");
        return false;
    }
    hdc_ = GetDC(hwnd_);
    if (!createContext(debugContext)) return false;

    // Raw input: mouse movement straight from the device, without Windows'
    // pointer acceleration and not stopped by the screen edge. Ideal for mouse-look.
    RAWINPUTDEVICE mouse{};
    mouse.usUsagePage = 0x01;  // generic desktop controls
    mouse.usUsage = 0x02;      // mouse
    mouse.hwndTarget = hwnd_;
    RegisterRawInputDevices(&mouse, 1, sizeof(mouse));

    QueryPerformanceFrequency(&timerFrequency_);
    QueryPerformanceCounter(&timerStart_);

    ShowWindow(hwnd_, SW_SHOW);
    SetForegroundWindow(hwnd_);  // Windows may refuse if another program is in use
    focused_ = (GetForegroundWindow() == hwnd_);
    RECT client{};
    GetClientRect(hwnd_, &client);
    width_ = client.right;
    height_ = client.bottom;
    return true;
}

bool Window::createContext(bool debugContext) {
    // Step 1: describe the pixels we want (32-bit colour, 24-bit depth buffer,
    // double buffered) and let Windows pick the closest format it has.
    PIXELFORMATDESCRIPTOR pfd{};
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cAlphaBits = 8;
    pfd.cDepthBits = 24;
    pfd.cStencilBits = 8;
    pfd.iLayerType = PFD_MAIN_PLANE;
    const int format = ChoosePixelFormat(hdc_, &pfd);
    if (format == 0 || !SetPixelFormat(hdc_, format, &pfd)) {
        showError("Could not set an OpenGL pixel format.");
        return false;
    }

    // Step 2: a temporary old-style context. Its only job is to let us look up
    // wglCreateContextAttribsARB, the function that makes MODERN contexts.
    HGLRC legacy = wglCreateContext(hdc_);
    if (!legacy || !wglMakeCurrent(hdc_, legacy)) {
        showError("Could not create an OpenGL context. Is the graphics driver installed?");
        return false;
    }
    auto wglCreateContextAttribsARB =
        reinterpret_cast<PFNWGLCREATECONTEXTATTRIBSARBPROC>(getOpenGLProc("wglCreateContextAttribsARB"));

    // Step 3: the real context: OpenGL 3.3, "core profile" (no deprecated 1990s features).
    if (wglCreateContextAttribsARB) {
        const int attributes[] = {
            WGL_CONTEXT_MAJOR_VERSION_ARB, 3,
            WGL_CONTEXT_MINOR_VERSION_ARB, 3,
            WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
            WGL_CONTEXT_FLAGS_ARB, debugContext ? WGL_CONTEXT_DEBUG_BIT_ARB : 0,
            0  // end of list
        };
        context_ = wglCreateContextAttribsARB(hdc_, nullptr, attributes);
    }
    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(legacy);
    if (!context_ || !wglMakeCurrent(hdc_, context_)) {
        showError("This PC's graphics driver does not support OpenGL 3.3.");
        return false;
    }

    // Step 4: fetch all the modern OpenGL functions.
    std::string missing;
    if (!loadOpenGLFunctions(missing)) {
        showError("The graphics driver is missing OpenGL functions:\n" + missing);
        return false;
    }

    // Step 5 (optional): wait for the monitor's refresh before showing each frame
    // ("v-sync"). This caps the frame rate to e.g. 60 fps and stops tearing.
    auto wglSwapIntervalEXT = reinterpret_cast<PFNWGLSWAPINTERVALEXTPROC>(getOpenGLProc("wglSwapIntervalEXT"));
    if (wglSwapIntervalEXT) wglSwapIntervalEXT(1);

    if (debugContext && glDebugMessageCallback) {
        glEnable(GL_DEBUG_OUTPUT);
        glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);  // report inside the guilty call, so a debugger shows where
        glDebugMessageCallback(openglDebugCallback, nullptr);
    }
    return true;
}

double Window::time() const {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return static_cast<double>(now.QuadPart - timerStart_.QuadPart) / static_cast<double>(timerFrequency_.QuadPart);
}

bool Window::processMessages() {
    // Per-frame values start again from zero.
    std::fill(std::begin(input_.pressed), std::end(input_.pressed), false);
    std::fill(std::begin(input_.mousePressed), std::end(input_.mousePressed), false);
    input_.mouseDX = input_.mouseDY = 0.0f;
    input_.wheel = 0;

    // PeekMessage (unlike GetMessage) does not wait when there are no messages,
    // so the game keeps running between key presses.
    MSG msg;
    while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) closed_ = true;
        TranslateMessage(&msg);
        DispatchMessageA(&msg);  // -> windowProc
    }
    return !closed_;
}

void Window::clearInput() {
    const int wheel = input_.wheel;
    input_ = InputState{};
    input_.wheel = wheel;
}

void Window::lockCursor() {
    // Pin the (hidden) cursor to the window's centre so it cannot wander onto
    // another window. Raw input still reports how far the mouse moved.
    RECT r{};
    GetClientRect(hwnd_, &r);
    POINT center{(r.left + r.right) / 2, (r.top + r.bottom) / 2};
    ClientToScreen(hwnd_, &center);
    RECT lock{center.x, center.y, center.x + 1, center.y + 1};
    ClipCursor(&lock);
}

void Window::setMouseCaptured(bool captured) {
    if (captured && !focused_) return;  // never trap the cursor while another window is in use
    if (captured == captured_) return;
    captured_ = captured;
    if (captured) {
        lockCursor();
        SetCursor(nullptr);
    } else {
        ClipCursor(nullptr);
        SetCursor(LoadCursorA(nullptr, IDC_ARROW));
    }
}

void Window::toggleFullscreen() {
    // "Borderless fullscreen": remove the border and cover the whole monitor.
    // (Raymond Chen's well-known recipe from "The Old New Thing".)
    const LONG_PTR style = GetWindowLongPtrA(hwnd_, GWL_STYLE);
    if (!fullscreen_) {
        MONITORINFO monitor{};
        monitor.cbSize = sizeof(monitor);
        savedPlacement_.length = sizeof(savedPlacement_);
        if (GetWindowPlacement(hwnd_, &savedPlacement_) &&
            GetMonitorInfoA(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTOPRIMARY), &monitor)) {
            SetWindowLongPtrA(hwnd_, GWL_STYLE, style & ~static_cast<LONG_PTR>(WS_OVERLAPPEDWINDOW));
            const RECT& m = monitor.rcMonitor;
            SetWindowPos(hwnd_, HWND_TOP, m.left, m.top, m.right - m.left, m.bottom - m.top,
                         SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
            fullscreen_ = true;
        }
    } else {
        SetWindowLongPtrA(hwnd_, GWL_STYLE, style | WS_OVERLAPPEDWINDOW);
        SetWindowPlacement(hwnd_, &savedPlacement_);
        SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        fullscreen_ = false;
    }
    if (captured_) lockCursor();
}

void Window::setMouseButton(int button, bool down) {
    if (down && !input_.mouseDown[button]) input_.mousePressed[button] = true;
    input_.mouseDown[button] = down;
}

LRESULT CALLBACK Window::windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    Window* self = nullptr;
    if (message == WM_NCCREATE) {
        // The very first message. CreateWindowEx's last argument (`this`) is
        // inside CREATESTRUCT; store it in the window so later messages find it.
        auto* create = reinterpret_cast<CREATESTRUCTA*>(lParam);
        self = static_cast<Window*>(create->lpCreateParams);
        SetWindowLongPtrA(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
    } else {
        self = reinterpret_cast<Window*>(GetWindowLongPtrA(hwnd, GWLP_USERDATA));
    }
    if (self) return self->handleMessage(message, wParam, lParam);
    return DefWindowProcA(hwnd, message, wParam, lParam);
}

LRESULT Window::handleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_CLOSE:  // the X button or Alt+F4
            closed_ = true;
            return 0;

        case WM_SIZE:
            width_ = LOWORD(lParam);
            height_ = HIWORD(lParam);
            if (captured_) lockCursor();
            return 0;

        case WM_MOVE:
            if (captured_) lockCursor();
            return 0;

        case WM_ERASEBKGND:
            return 1;  // OpenGL paints everything; skipping the erase avoids flicker

        case WM_SETFOCUS:
            focused_ = true;
            return 0;

        case WM_KILLFOCUS:  // Alt+Tab or a click on another window
            focused_ = false;
            setMouseCaptured(false);
            clearInput();  // otherwise a key held during Alt+Tab would stay "down" forever
            return 0;

        case WM_SETCURSOR:
            if (captured_ && LOWORD(lParam) == HTCLIENT) {
                SetCursor(nullptr);  // hide the arrow while playing
                return TRUE;
            }
            break;

        case WM_INPUT: {
            RAWINPUT raw{};
            UINT size = sizeof(raw);
            if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, &raw, &size,
                                sizeof(RAWINPUTHEADER)) != static_cast<UINT>(-1) &&
                raw.header.dwType == RIM_TYPEMOUSE && !(raw.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE) &&
                captured_) {
                input_.mouseDX += static_cast<float>(raw.data.mouse.lLastX);
                input_.mouseDY += static_cast<float>(raw.data.mouse.lLastY);
            }
            break;  // DefWindowProc must still see WM_INPUT to clean up
        }

        case WM_KEYDOWN:
        case WM_SYSKEYDOWN: {
            const int key = static_cast<int>(wParam & 0xFF);
            const bool wasDown = (lParam & (1 << 30)) != 0;  // bit 30: key was already down (auto-repeat)
            if (!wasDown) input_.pressed[key] = true;
            input_.down[key] = true;
            if (message == WM_SYSKEYDOWN) break;  // let Windows handle Alt+F4 and friends
            return 0;
        }

        case WM_KEYUP:
        case WM_SYSKEYUP:
            input_.down[wParam & 0xFF] = false;
            if (message == WM_SYSKEYUP) break;
            return 0;

        case WM_LBUTTONDOWN: setMouseButton(0, true); return 0;
        case WM_LBUTTONUP:   setMouseButton(0, false); return 0;
        case WM_RBUTTONDOWN: setMouseButton(1, true); return 0;
        case WM_RBUTTONUP:   setMouseButton(1, false); return 0;
        case WM_MBUTTONDOWN: setMouseButton(2, true); return 0;
        case WM_MBUTTONUP:   setMouseButton(2, false); return 0;

        case WM_MOUSEWHEEL: {
            // One "notch" is 120 units; smooth-scrolling mice send smaller steps.
            wheelRemainder_ += GET_WHEEL_DELTA_WPARAM(wParam);
            while (wheelRemainder_ >= WHEEL_DELTA) { input_.wheel++; wheelRemainder_ -= WHEEL_DELTA; }
            while (wheelRemainder_ <= -WHEEL_DELTA) { input_.wheel--; wheelRemainder_ += WHEEL_DELTA; }
            return 0;
        }
    }
    return DefWindowProcA(hwnd_, message, wParam, lParam);
}
