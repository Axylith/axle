#include "core/types.h"
#include "window.h"
#include "core/monitor.h"
#include "vulkan_init.h"
#include "swapchain.h"
#include "font.h"
#include "atlas.h"
#include "text.h"
#include <cstdio>
#include <X11/keysym.h>
#include <X11/Xlib.h>
#include <unistd.h>
#include <thread>
#include <csetjmp>
#include "editor.h"
#include "core/metrics.h"
#include <clocale>
#include <sys/select.h>
#include <sys/stat.h>

#include  "command.h"

static jmp_buf x_error_jmp;
static bool x_connection_lost = false;


static int x_io_error_handler(Display*) {
    x_connection_lost = true;
    longjmp(x_error_jmp, 1);
    return 0;  // never reached
}

static void render_and_sync(VulkanState& vk, AppWindow& app, const AxylFont& font,
                            Editor& editor, float ox, float oy) {
    render_frame(vk.renderer, vk.vkdev, vk.swapchain, vk.pipeline,
                 vk.text, vk.atlas, vk.solid, font, editor, ox, oy);
    if (app.sync_pending) {
        vkWaitForFences(vk.vkdev.device, 1, &vk.renderer.in_flight,
                        VK_TRUE, UINT64_MAX);
        XSyncSetCounter(app.display, app.sync_counter, app.sync_value);
        app.sync_pending = false;
        XFlush(app.display);
    }
}

int main(int argc, char** argv){
    setlocale(LC_ALL, "");
    XSetLocaleModifiers("");
    XInitThreads();

    Timer t;
    Timer total;
    Editor editor;
    editor_ensure_data_dir(editor);
    Metrics metrics;
    Timer   frame_timer;
    float offset_x = 0.25; //0 = right border
    float offset_y = 0.20; //0 = top border

    if (argc > 1) {
        editor.path = argv[1];

    }


    double last_sample_ms = 0.0;
    constexpr double MONITOR_INTERVAL_MS = 1000.0;
    double last_resize_time_ms = 0.0;
    bool resize_pending = false;

    AppWindow app = create_window(1280, 720);
    t.log("X11 window"); fflush(stdout);
    printf("[main] app.running=%d, app.width=%d, app.height=%d\n",
           (int)app.running, app.width, app.height);
    fflush(stdout);
    if(!app.running) return 1;

    // Create surface on main thread (touches X11)
    VkInstance instance = create_vulkan_instance();
    t.log("Vulkan instance"); fflush(stdout);
    if(instance == VK_NULL_HANDLE) return 1;


    VkSurfaceKHR surface = create_surface(instance, app);
    t.log("Vulkan surface"); fflush(stdout);
    if(surface == VK_NULL_HANDLE) return 1;


    ResourceMonitor monitor;
    monitor.open("axylith_resource.csv");

    VulkanState vk;
    vk.instance = instance;
    vk.surface = surface;


    std::thread init_thread([&vk, &app, &total]() {
        vk.gpu = pick_gpu(vk.instance, vk.surface);
        if (vk.gpu.device == VK_NULL_HANDLE) { vk.failed.store(true); return; }

        vk.vkdev = create_device(vk.gpu);
        if (vk.vkdev.device == VK_NULL_HANDLE) { vk.failed = true; return; }

        vk.swapchain = create_swapchain(vk.vkdev.device, vk.gpu.device, vk.surface, app, vk.gpu);
        if (vk.swapchain.handle == VK_NULL_HANDLE) { vk.failed = true; return; }

        vk.pipeline = create_pipeline(vk.vkdev.device, vk.swapchain.format);
        if (vk.pipeline.handle == VK_NULL_HANDLE) { vk.failed = true; return; }

        vk.renderer = create_renderer(vk.vkdev, vk.gpu, vk.pipeline);

        vk.atlas = create_atlas(vk.vkdev, vk.gpu, exe_relative("assets/jetbrains_mono.bin"));
        if (vk.atlas.image == VK_NULL_HANDLE) {
            fprintf(stderr, "[atlas] Failed to create atlas\n");
            vk.failed = true;
            return;
        }


        // NEW: Create text pipeline
        vk.text = create_text_pipeline(vk.vkdev.device,
                                       vk.swapchain.format,
                                       vk.atlas.set_layout,
                                       vk.gpu,
                                       1024);  // max 1024 glyphs

        vk.solid = create_solid_pipeline(vk.vkdev.device,
                                 vk.swapchain.format,
                                 vk.gpu,
                                 256);
        if (vk.text.handle == VK_NULL_HANDLE) {
            fprintf(stderr, "[text] Failed to create text pipeline\n");
            vk.failed = true;
            return;
        }

        printf("[timer] %-40s %.3f ms\n", "TOTAL INIT", total.elapsed_ms());
        vk.ready = true;


    });

    Timer monitor_timer;

    AxylFont font;
    if (font_load_metadata(font, exe_relative("assets/jetbrains_mono.json"))) {
        // Sample a few glyphs
        const Glyph* a = font_get_glyph(font, 'A');
        const Glyph* hash = font_get_glyph(font, '#');
        const Glyph* space = font_get_glyph(font, ' ');
        if (a) {
            printf("[test] 'A': advance=%.3f plane=(%.3f,%.3f,%.3f,%.3f) atlas=(%.0f,%.0f,%.0f,%.0f)\n",
                   a->advance, a->plane_left, a->plane_bottom, a->plane_right, a->plane_top,
                   a->atlas_left, a->atlas_bottom, a->atlas_right, a->atlas_top);
        }
        if (hash) printf("[test] '#': advance=%.3f\n", hash->advance);
        if (space) printf("[test] ' ': advance=%.3f (whitespace, no quad)\n", space->advance);
    }
    if (argc > 1) {
        editor_load(editor);
    }


    // Wait for init to finish so vk.text exists
    while (!vk.ready.load() && !vk.failed.load()) {
        usleep(1000);
    }
    if (vk.failed.load()) {
        fprintf(stderr, "[main] Init failed, exiting\n");
        return 1;
    }

   XSetIOErrorHandler(x_io_error_handler);

    // Set jump point — longjmp returns here with value 1
    if (setjmp(x_error_jmp) != 0) {
        // We got here from longjmp — X connection is dead
        printf("[x11] Connection lost, shutting down cleanly\n");
        fflush(stdout);
        goto cleanup;
    }


    while (app.running){

            // Per-frame viewport math — used by event handlers and the render block.
        const float TEXT_PIXEL_SIZE = 18.0f;
        const float line_height = (font.ascender - font.descender) * TEXT_PIXEL_SIZE * 1.2f;
        const float viewport_top_px = ((float)((1 + (offset_y-1))/2)*app.height);
        const float viewport_height_px = (float)app.height - viewport_top_px - 32.0f;
        const float text_origin_x = ((float)((1 + (offset_x-1))/2)*app.width);
        const float text_origin_y = ((float)((1 + (offset_y-1))/2)*app.height) - editor.scroll_y;
        // Document height for scroll clamping.
        size_t line_count = 1;
        for (char c : editor.text) if (c == '\n') line_count++;

        bool needs_redraw = editor.dirty || resize_pending;
        const float text_total_height_px = line_count * line_height;
        if (editor_get_status(editor, 3.0) != nullptr) needs_redraw = true;



        while (XPending(app.display)){
            XEvent ev;
            XNextEvent(app.display, &ev);

            switch (ev.type){
                case KeyPress:
                    {
                        char buf[64];
                        KeySym keysym = 0;
                        Status status = 0;
                        int n = Xutf8LookupString(app.xic, &ev.xkey,
                                                  buf, sizeof(buf) - 1,
                                                  &keysym, &status);
                        buf[n] = '\0';

                        bool ctrl = (ev.xkey.state & ControlMask) != 0;
                        bool handled = false;

                        if (status == XLookupKeySym || status == XLookupBoth)
                        {
                            handled = command_handle_key(keysym, ev.xkey.state, editor, metrics,app, viewport_top_px, viewport_height_px, line_height);
                        }
                        if (!handled && (status == XLookupChars || status == XLookupBoth)) {
                            if (!ctrl) {
                                if (editor.has_selection) {
                                    editor_record_edit(editor, EditKind::DeleteSel);
                                    editor_delete_selection(editor);
                                } else {
                                    editor_record_edit(editor, EditKind::Insert);
                                }
                                editor_insert_utf8(editor, buf, n);
                                editor_scroll_to_cursor(editor, viewport_top_px, viewport_height_px, line_height);
                            }
                        }

                        break;
                    }
                case ButtonPress: {
                    if (ev.xbutton.button == Button4) {
                        editor_scroll_lines(editor, -3, line_height, viewport_top_px,
                                            viewport_height_px, text_total_height_px);
                    } else if (ev.xbutton.button == Button5) {
                        editor_scroll_lines(editor, 3, line_height, viewport_top_px,
                                            viewport_height_px, text_total_height_px);
                    }
                    break;
                }


                case ClientMessage: {
                    printf("[x11] ClientMessage msg_type=%lu data[0]=%ld\n",
                           (unsigned long)ev.xclient.message_type,
                           (long)ev.xclient.data.l[0]);
                    fflush(stdout);

                    if (ev.xclient.message_type == app.wm_protocols) {
                        printf("[x11]   matched WM_PROTOCOLS, data[0]=%ld vs wm_delete=%lu\n",
                               (long)ev.xclient.data.l[0],
                               (unsigned long)app.wm_delete);
                        fflush(stdout);
                        if ((Atom)ev.xclient.data.l[0] == app.wm_delete) {
                            printf("[x11]   closing window\n");
                            fflush(stdout);
                            app.running = false;
                        } else if ((Atom)ev.xclient.data.l[0] == app.net_wm_sync_request){
                            XSyncValue v;
                            XSyncIntsToValue(&v, (unsigned int)ev.xclient.data.l[2], (int)ev.xclient.data.l[3]);
                            app.sync_value = v;
                            app.sync_pending = true;
                        }
                    }
                    break;
                }



                case ConfigureNotify: {
                    if (ev.xconfigure.width != app.width || ev.xconfigure.height != app.height) {
                        app.width = ev.xconfigure.width;
                        app.height = ev.xconfigure.height;
                        last_resize_time_ms = monitor_timer.elapsed_ms();
                        resize_pending = true;

                        // Acknowledge sync_request immediately — don't make WM wait for a render
                        if (app.sync_pending) {
                            XSyncSetCounter(app.display, app.sync_counter, app.sync_value);
                            app.sync_pending = false;
                            XFlush(app.display);
                        }
                    }
                    break;
                }
            }
        }

        if (vk.ready.load()) {
            // Rebuild editor text only when the buffer changed.
            if (editor.dirty) {
                build_text_vertices_with_cursor(vk.text, font,
                    editor.text.c_str(), editor.cursor,
                    text_origin_x, text_origin_y, 18.0f,    // x=50, baseline=80, 18px text
                    0.910f, 0.886f, 0.839f, 1.0f);
                append_cursor_quad(vk.text, font, 0.784f, 0.608f, 0.353f, 1.0f);
                editor.dirty = false;
            } else {
                // No editor change — but we still need to reset glyph_count
                // before appending the HUD this frame. Re-run with same text.
                build_text_vertices_with_cursor(vk.text, font,
                    editor.text.c_str(), editor.cursor,
                    text_origin_x, text_origin_y, 18.0f,    // x=50, baseline=80, 18px text
                    0.910f, 0.886f, 0.839f, 1.0f);

                append_cursor_quad(vk.text, font, 0.784f, 0.608f, 0.353f, 1.0f);
            }

            // Append HUD line at bottom, 14px, dimmed.
            if (metrics.visible) {
                char hud[256];
                metrics.glyph_count = vk.text.glyph_count;
                metrics.format(hud, sizeof(hud));

                float saved_pen   = vk.text.pen_x;
                float saved_base  = vk.text.baseline_y;
                float saved_size  = vk.text.pixel_size;

                // Line 1 (above): filename + modified marker + status message
                char title[160];
                const char* st = editor_get_status(editor, 3.0);
                snprintf(title, sizeof(title), "%s%s%s%s",
                         editor.path.c_str(),
                         editor.modified ? "*" : "",
                         st ? "  -  " : "",
                         st ? st : "");
                append_text_run(vk.text, font, title,
                                12.0f, (float)app.height - 28.0f, 14.0f,
                                0.545f, 0.518f, 0.478f, 0.9f);

                // Line 2 (below): perf metrics
                append_text_run(vk.text, font, hud,
                                12.0f, (float)app.height - 8.0f, 14.0f,
                                0.545f, 0.518f, 0.478f, 0.9f);

                vk.text.pen_x      = saved_pen;
                vk.text.baseline_y = saved_base;
                vk.text.pixel_size = saved_size;
            }
        }

        if (resize_pending) {
            double now = monitor_timer.elapsed_ms();
            if ((now - last_resize_time_ms) >= 50.0) {
                if (vk.ready.load()) {
                    recreate_swapchain(vk, app);
                    //render_and_sync(vk, app);
                }
                resize_pending = false;
            }
        } else if (vk.ready.load()) {
            if (vk.swapchain_dirty) {
                recreate_swapchain(vk, app);
                vk.swapchain_dirty = false;
            }
            //render_and_sync(vk, app, font, editor);
            if (vk.renderer.swapchain_dirty_local) {
                vk.swapchain_dirty = true;
                vk.renderer.swapchain_dirty_local = false;
            }
        } else {
            usleep(16000);   // pre-ready idle
        }

        double frame_ms = frame_timer.elapsed_ms();
        frame_timer = Timer{};
        metrics.on_frame(frame_ms);

        if (editor.measure_pending) {
            auto now = std::chrono::steady_clock::now();
            auto us  = std::chrono::duration_cast<std::chrono::microseconds>(
                         now - editor.last_input).count();
            metrics.on_input((double)us);
            editor.measure_pending = false;
        }

        if (vk.failed) {
            fprintf(stderr, "[vulkan] Init failed\n");
            app.running = false;
        }

        double now_ms = monitor_timer.elapsed_ms();
        if (now_ms - last_sample_ms >= MONITOR_INTERVAL_MS) {
            monitor.sample(now_ms);
            last_sample_ms = now_ms;
        }

        if (!vk.ready) {
            XFlush(app.display);
                int xfd = ConnectionNumber(app.display);
                fd_set fds;
                FD_ZERO(&fds);
                FD_SET(xfd, &fds);
                if (metrics.visible) {
                    struct timeval tv = { 0, 500000 };
                    select(xfd + 1, &fds, nullptr, nullptr, &tv);
                } else {
                    select(xfd + 1, &fds, nullptr, nullptr, nullptr);
                }
        } else {
            if (vk.swapchain_dirty) {
                recreate_swapchain(vk, app);
                vk.swapchain_dirty = false;
                needs_redraw = true;
            }

            static double last_hud_redraw_ms = 0.0;
            if (metrics.visible){
                double now = frame_timer.elapsed_ms();

                double t= monitor_timer.elapsed_ms();

                if(t - last_hud_redraw_ms >= 10){
                    needs_redraw = true;
                    last_hud_redraw_ms = t;
                }
            }
            if (needs_redraw){
                render_and_sync(vk, app, font, editor, text_origin_x, text_origin_y);
                if(vk.renderer.swapchain_dirty_local){
                    vk.swapchain_dirty = true;
                    vk.renderer.swapchain_dirty_local = false;
                }
            } else {
                XFlush(app.display);
                int xfd = ConnectionNumber(app.display);
                fd_set fds;
                FD_ZERO(&fds);
                FD_SET(xfd, &fds);
                if (metrics.visible) {
                    struct timeval tv = { 0, 500000 };
                    select(xfd + 1, &fds, nullptr, nullptr, &tv);
                } else {
                    select(xfd + 1, &fds, nullptr, nullptr, nullptr);
                }
            }
            //render_and_sync(vk, app, font, editor, text_origin_x, text_origin_y);
        }
    }

cleanup:
    monitor.close();

    init_thread.join();

    if (vk.ready) {
        vkDeviceWaitIdle(vk.vkdev.device);
        vkDestroyPipeline(vk.vkdev.device, vk.pipeline.handle, nullptr);
        vkDestroyPipelineLayout(vk.vkdev.device, vk.pipeline.layout, nullptr);
        vkDestroyBuffer(vk.vkdev.device, vk.renderer.vertex_buffer, nullptr);
        vkFreeMemory(vk.vkdev.device, vk.renderer.vertex_memory, nullptr);
        vkDestroySemaphore(vk.vkdev.device, vk.renderer.render_finished, nullptr);
        vkDestroySemaphore(vk.vkdev.device, vk.renderer.image_available, nullptr);
        vkDestroyFence(vk.vkdev.device, vk.renderer.in_flight, nullptr);
        vkDestroyCommandPool(vk.vkdev.device, vk.renderer.command_pool, nullptr);
        vkDeviceWaitIdle(vk.vkdev.device);

        if (vk.pending_destroy_swapchain != VK_NULL_HANDLE) {
            for (uint32_t i = 0; i < vk.pending_destroy_count; i++) {
                vkDestroyImageView(vk.vkdev.device, vk.pending_destroy_views[i], nullptr);
            }
            vkDestroySwapchainKHR(vk.vkdev.device, vk.pending_destroy_swapchain, nullptr);
        }

        for (uint32_t i = 0; i < vk.swapchain.image_count; i++) {
            vkDestroyImageView(vk.vkdev.device, vk.swapchain.views[i], nullptr);
        }
        vkDestroySwapchainKHR(vk.vkdev.device, vk.swapchain.handle, nullptr);
        
        destroy_solid_pipeline(vk.vkdev.device, vk.solid);

        destroy_text_pipeline(vk.vkdev.device, vk.text);

        destroy_atlas(vk.vkdev, vk.atlas);


        vkDestroyDevice(vk.vkdev.device, nullptr);
    }
    vkDestroySurfaceKHR(vk.instance, vk.surface, nullptr);
    vkDestroyInstance(vk.instance, nullptr);

    // Only touch X11 if connection is still alive
    if (!x_connection_lost) {
        XSyncDestroyCounter(app.display, app.sync_counter);
        if (app.xic) { XDestroyIC(app.xic); app.xic = nullptr; }
        if (app.xim) { XCloseIM(app.xim);  app.xim = nullptr; }
        XDestroyWindow(app.display, app.window);
        XSync(app.display, False);
        //Note: XCloseDisplay can race with NVIDIA driver shutdown
        //The OS reclaims the connection on process exit anyway
        //XCloseDisplay(app.display);
    }

    printf("[cleanup] Shutdown complete\n"); fflush(stdout);
    return 0;
}