#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <cstdio>
#include <vulkan/vulkan.h>
#include "window.h"


AppWindow create_window(int width, int height){
    AppWindow app{};


    // -- 1. Connect to X11 display server
    app.display = XOpenDisplay(NULL);
    
    if (!app.display) {
        fprintf(stderr, "Unable to connect to display\n");
        app.running = false;
        return app;
    }


    // Initialize XSync extension - required before any XSync* calls
    int sync_event_base, sync_error_base;
    int sync_major = 3, sync_minor = 1;
    if (!XSyncQueryExtension(app.display, &sync_event_base, &sync_error_base)) {
        fprintf(stderr, "[x11] XSync extension not available\n");
        app.running = false;
        return app;
    }
    if (!XSyncInitialize(app.display, &sync_major, &sync_minor)) {
        fprintf(stderr, "[x11] Failed to initialize XSync\n");
        app.running = false;
        return app;
    }
    printf("[x11] XSync initialized (%d.%d)\n", sync_major, sync_minor); fflush(stdout);
    // -- 2. Get Screen Information --
    int screen = DefaultScreen(app.display);


    app.window = XCreateSimpleWindow(
        app.display,                        // Connection to the Display server 
        DefaultRootWindow(app.display),     // Parent
        0,0,                        // x, y position
        width, height,              // Size
        1,                          // Border size
        WhitePixel(app.display, screen),    // Border color
        0
        //BlackPixel(app.display, screen)     // Background color
    );

    

    // ── 4. Set window title ──
    XStoreName(app.display, app.window, "Axylith");


    // ── 4. Set window title ──
    XSelectInput(app.display, app.window,
        KeyPressMask |
        ButtonPressMask |
        ExposureMask |
        StructureNotifyMask | FocusChangeMask
    );
        // Register WM_DELETE before mapping
    app.wm_delete = XInternAtom(app.display, "WM_DELETE_WINDOW", False);
    app.wm_protocols = XInternAtom(app.display, "WM_PROTOCOLS", False);
    app.net_wm_sync_request         = XInternAtom(app.display, "_NET_WM_SYNC_REQUEST", False);
    app.net_wm_sync_request_counter = XInternAtom(app.display, "_NET_WM_SYNC_REQUEST_COUNTER", False);
    Atom protocols[] = { app.wm_delete, app.net_wm_sync_request };
    XSetWMProtocols(app.display, app.window, protocols, 2);
    //XFlush(app.display);

    XSyncValue initial_value;
    XSyncIntToValue(&initial_value, 0);
    app.sync_counter = XSyncCreateCounter(app.display, initial_value);
    app.sync_pending = false;

    long counter_id = (long)app.sync_counter;
    XChangeProperty(app.display, app.window, app.net_wm_sync_request_counter, XA_CARDINAL, 32, PropModeReplace, (unsigned char*)&counter_id, 1);


    // ── 7. Show the window ──
    XMapWindow(app.display,app.window);

    printf("[axylith] Window created (%dx%d)\n", width, height);

    app.width = width;
    app.height = height;
    app.running = true;

    app.xim = XOpenIM(app.display, nullptr, nullptr, nullptr);
    if(!app.xim){
        fprintf(stderr, "[x11] XOpenIM failed (locale supported?)\n");
        app.running = false;
        return app;
    }

    app.xic = XCreateIC(app.xim, XNInputStyle, XIMPreeditNothing | XIMStatusNothing, XNClientWindow, app.window, XNFocusWindow, app.window, nullptr);

    if (!app.xic) {
        fprintf(stderr, "[x11] XCreateIC failed\n");
        XCloseIM(app.xim);
        app.xim = nullptr;
        app.running = false;
        return app;
    }
    XSetICFocus(app.xic);
    printf("[x11] XIM/XIC initialized\n"); fflush(stdout);

    return app;
    
}
