# 04 - Windows and input (`window.h`, `window.cpp`)

Every pixel and every key press passes through the Win32 API, the C interface Windows has had
since the 1990s. This chapter explains just enough of it to understand `window.cpp`, the only
file in the project that talks to Windows directly.

---

## 1. Handles

Windows hands you opaque numbers called **handles** instead of pointers to its internal objects:

| Handle | Is | Used for |
|---|---|---|
| `HINSTANCE` | this program (`GetModuleHandleA(nullptr)`) | registering the window class |
| `HWND` | a window | almost every window function |
| `HDC` | a *device context*: something you can draw into | pixel format, `SwapBuffers` |
| `HGLRC` | an OpenGL rendering context | `wglMakeCurrent`, `wglDeleteContext` |
| `HFONT`, `HBITMAP` | GDI objects | the HUD font (`hud.cpp`) |

Handles must be released with the matching function (`ReleaseDC`, `DeleteObject`,
`DestroyWindow` ...). That is exactly the RAII job the `Window` destructor does.

---

## 2. Creating a window

`Window::create` does this, in order:

1. **`SetProcessDPIAware()`**: see section 7.
2. **`RegisterClassExA`**: describes a *kind* of window. The important fields:
   - `lpfnWndProc = &Window::windowProc`: the function Windows calls with messages.
   - `style = CS_OWNDC`: the window keeps one private device context for life. OpenGL needs
     this, because the context is bound to that DC.
   - `hCursor`: the arrow shown over the window when it is not captured.
3. **`AdjustWindowRect`**: you ask for a *client area* (the inside, where we draw) of
   1280 x 720 scaled for DPI. This function adds the title bar and borders to get the outer
   size. The result is clamped to the screen's *work area* (the screen minus the taskbar) and
   centred.
4. **`CreateWindowExA(..., this)`**: makes the window. The final argument is any pointer you
   like; we pass `this` so `windowProc` can find our object (the "trampoline", see
   [03 §14](03-cpp-semantics.md)).
5. **`GetDC`**: gets the device context, then `createContext` sets up OpenGL
   ([05](05-opengl-pipeline.md)).
6. **`RegisterRawInputDevices`**: asks for raw mouse data (section 5).
7. **`ShowWindow` + `SetForegroundWindow`**: makes it visible and tries to bring it to the
   front.

**`A` or `W`?** Most Win32 functions come in two versions: `CreateWindowExA` takes `char`
strings in the system code page, `CreateWindowExW` takes UTF-16 `wchar_t` strings. This code
uses the `A` versions with plain English text, and the `W` versions where file paths are
involved (`GetModuleFileNameW`, `GetEnvironmentVariableW` in `game.cpp`), because a user's
folder name could contain any character.

---

## 3. The message loop

Windows tells a program about everything (key presses, mouse moves, resizing, closing) by
putting **messages** in its queue. A program must keep emptying the queue, or Windows marks it
"Not responding".

```cpp
// Window::processMessages
MSG msg;
while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
    TranslateMessage(&msg);   // turns key presses into WM_CHAR text messages (unused here, but standard)
    DispatchMessageA(&msg);   // calls our windowProc with this message
}
```

Ordinary programs use `GetMessage`, which **sleeps** until a message arrives. That is perfect for
a text editor and useless for a game, which must keep drawing frames even when nobody touches the
keyboard. `PeekMessage` returns immediately when the queue is empty, so the game loop handles
whatever messages arrived and then draws a frame.

---

## 4. The window procedure

`Window::handleMessage` is a `switch` over message types:

| Message | Sent when | What we do |
|---|---|---|
| `WM_CLOSE` | X button, Alt+F4 | set `closed_`; the main loop ends and saves |
| `WM_SIZE` | resized, maximised, minimised | store the new client size (0 x 0 when minimised) |
| `WM_MOVE` | window moved | re-lock the cursor if captured |
| `WM_ERASEBKGND` | Windows wants to paint the background | return 1 ("done") to avoid flicker; OpenGL paints everything |
| `WM_SETFOCUS` / `WM_KILLFOCUS` | keyboard focus gained / lost (e.g. Alt+Tab) | track `focused_`; on loss release the mouse and clear all keys |
| `WM_SETCURSOR` | the cursor moves over us | hide it while captured |
| `WM_INPUT` | raw mouse movement | add to `mouseDX/DY` |
| `WM_KEYDOWN` / `WM_KEYUP` | keys | update `down[]` / `pressed[]` |
| `WM_SYSKEYDOWN` / `WM_SYSKEYUP` | keys pressed with Alt, and F10 | same, then also pass to Windows so Alt+F4 works |
| `WM_LBUTTONDOWN` ... `WM_MBUTTONUP` | mouse buttons | update `mouseDown[]` / `mousePressed[]` |
| `WM_MOUSEWHEEL` | wheel | count notches |
| anything else | | `DefWindowProcA`: Windows' default behaviour |

**Return values matter.** Returning 0 means "handled". Breaking out of the switch to
`DefWindowProcA` means "do the default". Forgetting `DefWindowProcA` for unhandled messages makes
a window that cannot be moved, resized or closed.

---

## 5. Input

`InputState` (window.h) is the game's view of the keyboard and mouse:

```cpp
bool down[256];        // held right now
bool pressed[256];     // went down THIS frame (true for exactly one frame)
bool mouseDown[3], mousePressed[3];
float mouseDX, mouseDY;
int wheel;
```

**Down vs pressed.** Movement asks "is W *held*?" (`down['W']`). Toggles ask "was F *just
pressed*?" (`pressed['F']`), otherwise holding F for half a second would toggle flying 30 times.
`processMessages` clears `pressed[]` at the start of each frame, and a key-down message sets it.

**Auto-repeat.** Hold a key, and Windows sends `WM_KEYDOWN` again and again (that is how text
editors repeat letters). Bit 30 of `lParam` says "the key was already down", so repeats are
ignored:

```cpp
const bool wasDown = (lParam & (1 << 30)) != 0;
if (!wasDown) input_.pressed[key] = true;
```

**Virtual-key codes.** `wParam` of a key message is a *virtual-key code*. Letters and digits are
their capital ASCII values (`'W'`, `'1'`), and the rest have names: `VK_SPACE`, `VK_SHIFT`,
`VK_CONTROL`, `VK_ESCAPE`, `VK_F1`-`VK_F12`. Codes follow the key's *label*, so on a French AZERTY
keyboard "W" is in a different place. Real games use *scan codes* (physical positions) for
movement keys; see exercise 10.5.

**Clearing on focus loss.** If you Alt+Tab away while holding W, the key-up message goes to the
other program. Without `clearInput()` in `WM_KILLFOCUS`, the game would think W is still held
and walk you off a cliff when you come back.

### Raw mouse input for looking around

Normal mouse messages (`WM_MOUSEMOVE`) report the **cursor position**. That has two problems
for a camera: Windows applies *pointer acceleration* (fast flicks move further than slow ones),
and the cursor stops at the screen edge, so you could not keep turning.

**Raw input** reports how far the mouse itself moved, in sensor counts:

```cpp
RAWINPUTDEVICE mouse{};
mouse.usUsagePage = 0x01;  // "generic desktop controls" (USB HID usage tables)
mouse.usUsage = 0x02;      // "mouse"
mouse.hwndTarget = hwnd_;
RegisterRawInputDevices(&mouse, 1, sizeof(mouse));
```

After that, every movement produces a `WM_INPUT` message, and `GetRawInputData` returns
`lLastX` / `lLastY`. These are added up during the frame and used by `Player::turn`, multiplied
by the sensitivity (0.0022 radians per count, about 0.13°).

(Remote desktop and some virtual machines report *absolute* positions instead,
`MOUSE_MOVE_ABSOLUTE`. The code ignores those, so mouse-look does not work over RDP.)

### Capturing the mouse

While playing, the cursor must be invisible and must not wander out of the window (a click
outside would switch to another program). `setMouseCaptured(true)`:

- **`ClipCursor`** locks the cursor to a 1 x 1 pixel rectangle at the window centre. Raw input
  keeps reporting movement anyway.
- **`SetCursor(nullptr)`** hides it. `WM_SETCURSOR` keeps hiding it, because Windows tries to
  restore the arrow every time the mouse moves.

Capture is released by Esc, by losing focus, and when the window is destroyed (`~Window` calls
`ClipCursor(nullptr)`). A program that dies while holding a clip would leave the cursor trapped,
which is why the release is in the destructor.

> **A bug found during testing.** `focused_` used to start as `true`. When the game was started
> from a script while another program was in front, Windows refused to give it focus, but the
> game still believed it had it and locked the cursor of the program you were using. Now
> `focused_` is read from `GetForegroundWindow()` after the window appears, and
> `setMouseCaptured(true)` refuses to capture without focus. The lesson: never assume a state
> that you can ask the OS for.

---

## 6. Timing

```cpp
QueryPerformanceFrequency(&timerFrequency_);   // ticks per second (fixed at boot)
QueryPerformanceCounter(&now);                 // ticks since boot
seconds = (now - timerStart_) / frequency;
```

The *performance counter* is the most precise clock Windows has (sub-microsecond). `main.cpp`
computes `dt` from it every frame.

**V-sync** (`wglSwapIntervalEXT(1)` in `createContext`) makes `SwapBuffers` wait for the
monitor's next refresh. The game therefore runs at exactly the monitor rate (60, 120, 144 Hz ...)
without measuring or sleeping. When the window is minimised there is nothing to draw and
`SwapBuffers` would return at once, so `main.cpp` calls `Sleep(10)` instead to avoid using 100%
of a CPU core.

---

## 7. High-DPI screens

Laptop screens often run at 125% or 150% scaling. A program that does not declare itself
**DPI-aware** is drawn small and then stretched by Windows, which makes it blurry.
`SetProcessDPIAware()` says "I handle scaling myself". Then:

```cpp
uiScale_ = GetDeviceCaps(screen, LOGPIXELSY) / 96.0f;   // 96 DPI = 100%; 120 = 125%; 144 = 150%
```

and the window size, font size (16 px x scale), hotbar slots and crosshair are all multiplied by
`uiScale`. Your laptop runs at 125%, which is why the test screenshots are 1600 x 900
(1280 x 720 x 1.25).

---

## 8. Borderless fullscreen (F11)

`toggleFullscreen` follows Raymond Chen's well-known recipe (from Microsoft's blog *The Old New
Thing*):

1. Save the window's current position with `GetWindowPlacement`.
2. Remove `WS_OVERLAPPEDWINDOW` (title bar and borders) from its style.
3. Resize it to cover the whole monitor (`GetMonitorInfo` → `rcMonitor`) with
   `SWP_FRAMECHANGED`, so Windows re-reads the style.

Going back restores the style and the saved placement. This is "borderless windowed": Alt+Tab is
instant and nothing changes the display mode, which is why most modern games default to it.

---

## 9. Known limitation: the game freezes while you drag the window

When you drag the title bar or a border, Windows runs its own *modal loop* inside
`DispatchMessage`, and doesn't return to our loop until you let go. So the game pauses while you
drag. The usual fixes are to render from a `WM_TIMER` during the drag, or to run the game on a
second thread. For a game where you mostly play fullscreen, it is a reasonable trade-off.

---

## Try it

- Print every message: add `std::fprintf(stderr, "msg %04x\n", message);` at the top of
  `handleMessage`, build with `build.bat debug` and watch the flood when you move the mouse.
- Change the sensitivity in `game.cpp`, or add a key that flips the Y axis (multiply `dy` by -1).
- Read Microsoft's documentation for any function: search for "`FunctionName` win32".
