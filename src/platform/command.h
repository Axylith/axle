#pragma once
#include <X11/Xlib.h>
struct  Editor;
struct Metrics;
struct AppWindow;

bool command_handle_key(KeySym keysym, unsigned int state, Editor& editor, Metrics& metrics, AppWindow& app, float viewport_top_px, float viewport_height_px, float line_height);

