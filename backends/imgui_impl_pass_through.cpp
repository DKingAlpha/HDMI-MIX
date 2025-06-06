// dear imgui: Platform Binding for Android native app
// This needs to be used along with the OpenGL 3 Renderer (imgui_impl_opengl3)

// Implemented features:
//  [X] Platform: Keyboard support. Since 1.87 we are using the io.AddKeyEvent() function. Pass ImGuiKey values to all key functions e.g. ImGui::IsKeyPressed(ImGuiKey_Space). [Legacy AKEYCODE_* values are obsolete since 1.87 and not supported since 1.91.5]
//  [X] Platform: Mouse support. Can discriminate Mouse/TouchScreen/Pen.
// Missing features or Issues:
//  [ ] Platform: Clipboard support.
//  [ ] Platform: Gamepad support.
//  [ ] Platform: Mouse cursor shape and visibility (ImGuiBackendFlags_HasMouseCursors). Disable with 'io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange'. FIXME: Check if this is even possible with Android.
// Important:
//  - Consider using SDL or GLFW backend on Android, which will be more full-featured than this.
//  - FIXME: On-screen keyboard currently needs to be enabled by the application (see examples/ and issue #3446)
//  - FIXME: Unicode character inputs needs to be passed by Dear ImGui by the application (see examples/ and issue #3446)

// You can use unmodified imgui_impl_* files in your project. See examples/ folder for examples of using this.
// Prefer including the entire imgui/ repository into your project (either as a copy or as a submodule), and only build the backends you need.
// Learn about Dear ImGui:
// - FAQ                  https://dearimgui.com/faq
// - Getting Started      https://dearimgui.com/getting-started
// - Documentation        https://dearimgui.com/docs (same as your local docs/ folder).
// - Introduction, links and more at the top of imgui.cpp

// CHANGELOG
// (minor and older changes stripped away, please see git history for details)
//  2022-09-26: Inputs: Renamed ImGuiKey_ModXXX introduced in 1.87 to ImGuiMod_XXX (old names still supported).
//  2022-01-26: Inputs: replaced short-lived io.AddKeyModsEvent() (added two weeks ago) with io.AddKeyEvent() using ImGuiKey_ModXXX flags. Sorry for the confusion.
//  2022-01-17: Inputs: calling new io.AddMousePosEvent(), io.AddMouseButtonEvent(), io.AddMouseWheelEvent() API (1.87+).
//  2022-01-10: Inputs: calling new io.AddKeyEvent(), io.AddKeyModsEvent() + io.SetKeyEventNativeData() API (1.87+). Support for full ImGuiKey range.
//  2021-03-04: Initial version.

#include "imgui.h"
#ifndef IMGUI_DISABLE
#include "imgui_impl_pass_through.h"
#include <time.h>

static double                                   g_Time = 0.0;


void ImGui_ImplPassThrough_HandleInputEvent()
{
}

bool ImGui_ImplPassThrough_Init(int width, int height)
{
    IMGUI_CHECKVERSION();

    g_Time = 0.0;

    // Setup backend capabilities flags
    ImGuiIO& io = ImGui::GetIO();
    io.BackendPlatformName = "imgui_impl_rk_gles";
    io.DisplaySize = ImVec2((float)width, (float)height);
    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
    return true;
}

void ImGui_ImplPassThrough_Shutdown()
{
    ImGuiIO& io = ImGui::GetIO();
    io.BackendPlatformName = nullptr;
}

void ImGui_ImplPassThrough_NewFrame()
{
    // Setup time step
    struct timespec current_timespec;
    clock_gettime(CLOCK_MONOTONIC, &current_timespec);
    double current_time = (double)(current_timespec.tv_sec) + (current_timespec.tv_nsec / 1000000000.0);
    ImGuiIO& io = ImGui::GetIO();
    io.DeltaTime = g_Time > 0.0 ? (float)(current_time - g_Time) : (float)(1.0f / 60.0f);
    g_Time = current_time;
}

//-----------------------------------------------------------------------------

#endif // #ifndef IMGUI_DISABLE
