#include "command.h"
#include "editor.h"
#include "metrics.h"
#include "window.h"

#include <X11/keysym.h>
#include <X11/Xlib.h>

bool command_handle_key(KeySym keysym,
                        unsigned int state,
                        Editor& editor,
                        Metrics& metrics,
                        AppWindow& app,
                        float viewport_top_px,
                        float viewport_height_px,
                        float line_height)
{
    bool ctrl  = (state & ControlMask) != 0;
    bool shift = (state & ShiftMask)   != 0;

    if (ctrl) {
        switch (keysym) {
            case XK_s: case XK_S: editor_save(editor); return true;
            case XK_o: case XK_O: editor_load(editor); return true;

            case XK_Left:
                if (shift) {
                    if (!editor.has_selection) editor.sel_anchor = editor.cursor;
                    editor_move_word_left(editor);
                    editor_select_to(editor, editor.cursor);
                } else {
                    editor_clear_selection(editor);
                    editor_move_word_left(editor);
                }
                editor_scroll_to_cursor(editor, viewport_top_px, viewport_height_px, line_height);
                return true;

            case XK_Right:
                if (shift) {
                    if (!editor.has_selection) editor.sel_anchor = editor.cursor;
                    editor_move_word_right(editor);
                    editor_select_to(editor, editor.cursor);
                } else {
                    editor_clear_selection(editor);
                    editor_move_word_right(editor);
                }
                editor_scroll_to_cursor(editor, viewport_top_px, viewport_height_px, line_height);
                return true;

            case XK_BackSpace:
                if (editor.has_selection) {
                    editor_record_edit(editor, EditKind::DeleteSel);
                    editor_delete_selection(editor);
                } else {
                    editor_record_edit(editor, EditKind::DeleteSel);
                    editor_delete_word_left(editor);
                }
                editor_scroll_to_cursor(editor, viewport_top_px, viewport_height_px, line_height);
                return true;

            case XK_z: case XK_Z:
                if (shift) editor_redo(editor); else editor_undo(editor);
                editor_scroll_to_cursor(editor, viewport_top_px, viewport_height_px, line_height);
                return true;

            case XK_y: case XK_Y:
                editor_redo(editor);
                editor_scroll_to_cursor(editor, viewport_top_px, viewport_height_px, line_height);
                return true;

            case XK_c: case XK_C:
                editor_copy(editor);
                return true;

            case XK_x: case XK_X:
                editor_cut(editor);
                editor_scroll_to_cursor(editor, viewport_top_px, viewport_height_px, line_height);
                return true;

            case XK_v: case XK_V:
                editor_paste(editor);
                editor_scroll_to_cursor(editor, viewport_top_px, viewport_height_px, line_height);
                return true;

            case XK_a: case XK_A:
                editor_select_all(editor);
                return true;

            default: break;
        }
    }

    switch (keysym) {
        case XK_Escape:    app.running = false; return true;

        case XK_BackSpace:
            if (editor.has_selection) {
                editor_record_edit(editor, EditKind::DeleteSel);
                editor_delete_selection(editor);
            } else {
                editor_record_edit(editor, EditKind::Delete);
                editor_backspace(editor);
            }
            editor_scroll_to_cursor(editor, viewport_top_px, viewport_height_px, line_height);
            return true;

        case XK_Return:
            if (editor.has_selection) {
                editor_record_edit(editor, EditKind::DeleteSel);
                editor_delete_selection(editor);
            } else {
                editor_record_edit(editor, EditKind::Insert);
            }
            editor_newline(editor);
            editor_scroll_to_cursor(editor, viewport_top_px, viewport_height_px, line_height);
            return true;

        case XK_Tab:      editor_record_edit(editor, EditKind::Insert); editor_insert_utf8(editor, "\t", 1); return true;
        case XK_F1:   metrics.visible = !metrics.visible; return true;

        case XK_Left:
            if (shift) {
                if (!editor.has_selection) editor.sel_anchor = editor.cursor;
                editor_move_left(editor);
                editor_select_to(editor, editor.cursor);
            } else {
                editor_clear_selection(editor);
                editor_move_left(editor);
            }
            editor_scroll_to_cursor(editor, viewport_top_px, viewport_height_px, line_height);
            return true;

        case XK_Right:
            if (shift) {
                if (!editor.has_selection) editor.sel_anchor = editor.cursor;
                editor_move_right(editor);
                editor_select_to(editor, editor.cursor);
            } else {
                editor_clear_selection(editor);
                editor_move_right(editor);
            }
            editor_scroll_to_cursor(editor, viewport_top_px, viewport_height_px, line_height);
            return true;

        case XK_Up:    editor_move_up(editor);   return true;
        case XK_Down:  editor_move_down(editor); return true;
        case XK_Home:  editor_move_home(editor); return true;
        case XK_End:   editor_move_end(editor);  return true;

        default: return false;
    }
}