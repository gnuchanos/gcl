#ifndef GCL_RAYLIB_WINDOW_H
#define GCL_RAYLIB_WINDOW_H

#include <raylib.h>

static inline int gcl_raylib_window_init(int width, int height, const char *title) {
    if (IsWindowReady()) return -1;
    InitWindow(width, height, title);
    return IsWindowReady() ? 1 : 0;
}

static inline int gcl_raylib_window_close(void) {
    if (!IsWindowReady()) return 0;
    CloseWindow();
    return 1;
}

#endif
