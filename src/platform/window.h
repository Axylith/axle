#pragma once
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/sync.h>

struct AppWindow
{
    Display* display;
    Window window;
    Atom wm_delete;
    Atom wm_protocols;

    Atom net_wm_sync_request;
    Atom net_wm_sync_request_counter;
    XSyncCounter sync_counter;
    XSyncValue sync_value;
    bool sync_pending;

    XIM xim = nullptr;
    XIC xic = nullptr;

    int width;
    int height;
    bool running;
};

AppWindow create_window(int width, int height);