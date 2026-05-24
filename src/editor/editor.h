#pragma once
#include <string>
#include <vector>
#include <chrono>

enum class SaveFormat { Plain, Axl };

// Coarse classification of a mutation, used only to coalesce undo history:
// a run of consecutive same-kind edits collapses into one undo step, while a
// change of kind (typing -> deleting) forms a boundary. Paste and selection
// deletes are always their own step.
enum class EditKind { Initial, Insert, Delete, Paste, DeleteSel };

struct UndoState {
    std::string text;
    size_t      cursor = 0;
};


struct Editor {
    std::string text;
    bool dirty = true;
    bool modified = false;
    size_t cursor = 0;
    float scroll_y = 0.0f;

    size_t sel_anchor = 0;
    size_t sel_active = 0;
    bool   has_selection = false;

    std::string path = "../data/untitled.axl";

    std::string status;
    std::chrono::steady_clock::time_point status_set;


    std::chrono::steady_clock::time_point last_input;
    bool measure_pending = false;
    SaveFormat format = SaveFormat::Axl;

    // Undo/redo history. Snapshots are full-buffer copies — fine for the
    // 64MB-capped v1 editor; a piece table can replace this later without
    // touching callers.
    std::vector<UndoState> undo_stack;
    std::vector<UndoState> redo_stack;
    EditKind last_edit_kind = EditKind::Initial;

    // Internal copy/cut/paste register. The X11 system selection is layered
    // on top of this in a later step; this is the source of truth.
    std::string clipboard;

};

void editor_insert_utf8(Editor& e, const char* bytes, int n);
void editor_backspace(Editor& e);

bool editor_save (Editor& e);
bool editor_load (Editor& e);

void editor_set_status(Editor& e, const char* msg);
const char* editor_get_status(const Editor& e, double max_age_seconds = 3.0);


void editor_newline(Editor& e);
void editor_ensure_data_dir(const Editor& e);

void editor_move_left  (Editor& e);
void editor_move_right (Editor& e);
void editor_move_up    (Editor& e);
void editor_move_down  (Editor& e);
void editor_move_home  (Editor& e);
void editor_move_end   (Editor& e);

void editor_scroll_to_cursor(Editor& e,
                              float viewport_top_px,
                              float viewport_height_px,
                              float line_height_px);

void editor_scroll_lines(Editor& e, int n_lines, float line_height_px,
                         float viewport_top_px, float viewport_height_px,
                         float text_total_height_px);

void editor_page_up   (Editor& e, float viewport_height_px, float line_height_px);
void editor_page_down (Editor& e, float viewport_height_px, float line_height_px);

void editor_selection_range(const Editor& e, size_t& lo, size_t& hi);
void editor_clear_selection(Editor& e);
void editor_select_to(Editor& e, size_t new_cursor);
void editor_delete_selection(Editor& e);

void editor_move_word_left(Editor& e);
void editor_move_word_right(Editor& e);

void editor_delete_word_left(Editor& e);
void editor_delete_word_right(Editor& e);

// --- Undo / redo ---
// Call editor_record_edit() with the kind of the mutation that is ABOUT to
// happen, before performing it. It snapshots the pre-edit buffer when the
// edit starts a new undo step (kind boundary / paste / selection delete) and
// drops the redo history. editor_undo/redo swap between the stacks.
constexpr size_t EDITOR_UNDO_LIMIT = 256;
void editor_record_edit(Editor& e, EditKind kind);
bool editor_undo(Editor& e);
bool editor_redo(Editor& e);

// --- Clipboard ---
// copy: selection -> clipboard (no buffer change). cut: copy then delete the
// selection. paste: replace any selection with the clipboard contents.
// All are no-ops where they would do nothing (e.g. copy with no selection).
void editor_copy (Editor& e);
void editor_cut  (Editor& e);
void editor_paste(Editor& e);

void editor_record_edit(Editor& e, EditKind kind);
void editor_select_all(Editor& e);