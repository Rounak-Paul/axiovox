#include "window.h"
#include "log.h"
#include <GLFW/glfw3.h>
/** Reports GLFW diagnostics through the logger. */
static void error_callback(int code, const char *description) { ax_log(AX_LOG_ERROR, "GLFW %d: %s", code, description); }
/** Forwards framebuffer changes to the main-thread event bus. */
static void resize_callback(GLFWwindow *handle, int width, int height)
{
    AxWindow *window = glfwGetWindowUserPointer(handle);
    AxEvent event = {.type = AX_EVENT_WINDOW_RESIZE, .data.resize = {width, height}};
    ax_event_emit(window->events, &event);
}
/** Forwards user close requests to the event bus. */
static void close_callback(GLFWwindow *handle)
{
    AxWindow *window = glfwGetWindowUserPointer(handle);
    AxEvent event = {.type = AX_EVENT_WINDOW_CLOSE};
    ax_event_emit(window->events, &event);
}
bool ax_window_create(AxWindow *window, AxEvents *events, int width, int height)
{
    if (!window || width <= 0 || height <= 0) return false;
    *window = (AxWindow){.events = events};
    glfwSetErrorCallback(error_callback);
    glfwInitVulkanLoader(vkGetInstanceProcAddr);
    if (!glfwInit()) return false;
    if (!glfwVulkanSupported()) { glfwTerminate(); return false; }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    window->handle = glfwCreateWindow(width, height, "Axiovox", NULL, NULL);
    if (!window->handle) { glfwTerminate(); return false; }
    glfwSetWindowUserPointer(window->handle, window);
    glfwSetFramebufferSizeCallback(window->handle, resize_callback);
    glfwSetWindowCloseCallback(window->handle, close_callback);
    return true;
}
void ax_window_destroy(AxWindow *window)
{
    if (!window || !window->handle) return;
    glfwDestroyWindow(window->handle);
    window->handle = NULL;
    glfwTerminate();
}

/** Translates GLFW navigation keys to engine keys. */
static AxKey translate_key(int key)
{
    if (key >= GLFW_KEY_A && key <= GLFW_KEY_Z) return (AxKey)(AX_KEY_A + key - GLFW_KEY_A);
    if (key >= GLFW_KEY_0 && key <= GLFW_KEY_9) return (AxKey)(AX_KEY_0 + key - GLFW_KEY_0);
    if (key >= GLFW_KEY_F1 && key <= GLFW_KEY_F12) return (AxKey)(AX_KEY_F1 + key - GLFW_KEY_F1);
    switch (key) {
    case GLFW_KEY_SPACE: return AX_KEY_SPACE;
    case GLFW_KEY_LEFT_SHIFT: return AX_KEY_LEFT_SHIFT;
    case GLFW_KEY_RIGHT_SHIFT: return AX_KEY_RIGHT_SHIFT;
    case GLFW_KEY_LEFT_CONTROL: return AX_KEY_LEFT_CONTROL;
    case GLFW_KEY_RIGHT_CONTROL: return AX_KEY_RIGHT_CONTROL;
    case GLFW_KEY_LEFT_ALT: return AX_KEY_LEFT_ALT;
    case GLFW_KEY_RIGHT_ALT: return AX_KEY_RIGHT_ALT;
    case GLFW_KEY_LEFT_SUPER: return AX_KEY_LEFT_SUPER;
    case GLFW_KEY_RIGHT_SUPER: return AX_KEY_RIGHT_SUPER;
    case GLFW_KEY_SLASH: return AX_KEY_SLASH;
    case GLFW_KEY_ESCAPE: return AX_KEY_ESCAPE;
    case GLFW_KEY_ENTER: case GLFW_KEY_KP_ENTER: return AX_KEY_ENTER;
    case GLFW_KEY_BACKSPACE: return AX_KEY_BACKSPACE;
    case GLFW_KEY_DELETE: return AX_KEY_DELETE;
    case GLFW_KEY_LEFT: return AX_KEY_LEFT;
    case GLFW_KEY_RIGHT: return AX_KEY_RIGHT;
    case GLFW_KEY_HOME: return AX_KEY_HOME;
    case GLFW_KEY_END: return AX_KEY_END;
    case GLFW_KEY_UP: return AX_KEY_UP;
    case GLFW_KEY_DOWN: return AX_KEY_DOWN;
    case GLFW_KEY_TAB: return AX_KEY_TAB;
    default: return AX_KEY_UNKNOWN;
    }
}
/** Routes physical keyboard transitions independently of text input. */
static void key_callback(GLFWwindow *handle, int key, int scan, int action, int mods)
{
    (void)scan;
    AxWindow *window = glfwGetWindowUserPointer(handle);
    int modifiers = 0;
    if (mods & GLFW_MOD_SHIFT) modifiers |= AX_MOD_SHIFT;
    if (mods & GLFW_MOD_CONTROL) modifiers |= AX_MOD_CONTROL;
    if (mods & GLFW_MOD_ALT) modifiers |= AX_MOD_ALT;
    if (mods & GLFW_MOD_SUPER) modifiers |= AX_MOD_SUPER;
    ax_input_key(window->input, translate_key(key), action != GLFW_RELEASE, action == GLFW_REPEAT, modifiers);
}
/** Routes Unicode text produced by the OS keyboard layout. */
static void text_callback(GLFWwindow *handle, unsigned int codepoint)
{
    AxWindow *window = glfwGetWindowUserPointer(handle);
    ax_input_text(window->input, codepoint);
}
/** Releases held input on focus loss. */
static void focus_callback(GLFWwindow *handle, int focused)
{
    AxWindow *window = glfwGetWindowUserPointer(handle);
    ax_input_focus(window->input, focused != 0);
}
/** Updates pointer coordinates in logical window pixels. */
static void cursor_callback(GLFWwindow *handle, double x, double y)
{
    AxWindow *window = glfwGetWindowUserPointer(handle);
    window->input->mouse_x = x;
    window->input->mouse_y = y;
}
/** Updates held pointer button state. */
static void mouse_callback(GLFWwindow *handle, int button, int action, int mods)
{
    (void)mods;
    AxWindow *window = glfwGetWindowUserPointer(handle);
    if (button >= 0 && button < 8) window->input->mouse_down[button] = action != GLFW_RELEASE;
}
/** Accumulates scrolling within a frame. */
static void scroll_callback(GLFWwindow *handle, double x, double y)
{
    AxWindow *window = glfwGetWindowUserPointer(handle);
    window->input->scroll_x += x;
    window->input->scroll_y += y;
}
void ax_window_set_input(AxWindow *window, AxInput *input)
{
    if (!window || !window->handle || !input) return;
    window->input = input;
    input->events = window->events;
    glfwSetKeyCallback(window->handle, key_callback);
    glfwSetCharCallback(window->handle, text_callback);
    glfwSetWindowFocusCallback(window->handle, focus_callback);
    glfwSetCursorPosCallback(window->handle, cursor_callback);
    glfwSetMouseButtonCallback(window->handle, mouse_callback);
    glfwSetScrollCallback(window->handle, scroll_callback);
}
