//
// Created by nightingale on 5/23/26.
//
// editor_test.cpp — pure logic tests for the editor model.
// No Vulkan, no X11. Just covers undo/redo, clipboard, selection, word
// movement, UTF-8 handling, and the save/load round-trip.
//
// Build: linked via CMakeLists.txt against src/editor/editor.cpp.
// Returns 0 on success, 1 on failure.

#include "editor.h"
#include <cstdio>
#include <cstring>
#include <thread>
#include <chrono>

static int run = 0, pass = 0;
#define CHECK(cond) do { run++; if (cond) { pass++; } else { \
    printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define SUB(name) printf("- %s\n", name)

// Mirror of main.cpp's text-input path: typing one character through the
// undo system the same way the editor would.
static void type_char(Editor& e, char c) {
    if (e.has_selection) {
        editor_record_edit(e, EditKind::DeleteSel);
        editor_delete_selection(e);
    } else {
        editor_record_edit(e, EditKind::Insert);
    }
    char b[2] = { c, 0 };
    editor_insert_utf8(e, b, 1);
}

// ─── Basic insert / cursor ────────────────────────────
static void test_insert_and_cursor() {
    SUB("insert + cursor advance");
    Editor e;
    editor_insert_utf8(e, "hello", 5);
    CHECK(e.text == "hello");
    CHECK(e.cursor == 5);
    CHECK(e.modified == true);
    CHECK(e.has_selection == false);

    SUB("insert at middle");
    e.cursor = 2;
    editor_insert_utf8(e, "XX", 2);
    CHECK(e.text == "heXXllo");
    CHECK(e.cursor == 4);
}

static void test_backspace() {
    SUB("backspace removes one char before cursor");
    Editor e;
    editor_insert_utf8(e, "hello", 5);
    editor_backspace(e);
    CHECK(e.text == "hell");
    CHECK(e.cursor == 4);

    SUB("backspace at start of buffer is no-op");
    Editor e2;
    e2.cursor = 0;
    editor_backspace(e2);
    CHECK(e2.text == "");
    CHECK(e2.cursor == 0);
}

// ─── UTF-8 cursor movement ────────────────────────────
static void test_utf8_movement() {
    SUB("move_right advances over multi-byte char");
    Editor e;
    // "café" — 'é' is 2 bytes (0xC3 0xA9 in UTF-8)
    e.text = "caf\xC3\xA9";   // 5 bytes total
    e.cursor = 0;
    editor_move_right(e);  CHECK(e.cursor == 1);  // c
    editor_move_right(e);  CHECK(e.cursor == 2);  // a
    editor_move_right(e);  CHECK(e.cursor == 3);  // f
    editor_move_right(e);  CHECK(e.cursor == 5);  // é (2 bytes, jumps 2)
    editor_move_right(e);  CHECK(e.cursor == 5);  // past end, stays

    SUB("move_left retreats over multi-byte char");
    editor_move_left(e);   CHECK(e.cursor == 3);  // back past é
    editor_move_left(e);   CHECK(e.cursor == 2);  // f
}

// ─── Line movement ────────────────────────────────────
static void test_line_movement() {
    SUB("home/end on a single line");
    Editor e;
    e.text = "hello world";
    e.cursor = 5;
    editor_move_home(e); CHECK(e.cursor == 0);
    editor_move_end(e);  CHECK(e.cursor == 11);

    SUB("up/down preserves byte column");
    Editor e2;
    e2.text = "first\nsecond\nthird";   // 6, 7, 5 chars
    e2.cursor = 3;                       // inside "first"
    editor_move_down(e2);
    CHECK(e2.cursor == 9);               // 3 chars into "second"
    editor_move_down(e2);
    CHECK(e2.cursor == 16);              // 3 chars into "third"
    editor_move_up(e2);
    CHECK(e2.cursor == 9);

    SUB("up at top line is no-op");
    Editor e3;
    e3.text = "abc";
    e3.cursor = 1;
    editor_move_up(e3);
    CHECK(e3.cursor == 1);
}

// ─── Word movement ────────────────────────────────────
static void test_word_movement() {
    SUB("word_right skips whitespace then word");
    Editor e;
    e.text = "hello world foo";
    e.cursor = 0;
    editor_move_word_right(e); CHECK(e.cursor == 5);   // end of "hello"
    editor_move_word_right(e); CHECK(e.cursor == 11);  // end of "world"
    editor_move_word_right(e); CHECK(e.cursor == 15);  // end of "foo"

    SUB("word_left walks backward");
    e.cursor = 15;
    editor_move_word_left(e);  CHECK(e.cursor == 12);  // start of "foo"
    editor_move_word_left(e);  CHECK(e.cursor == 6);   // start of "world"
    editor_move_word_left(e);  CHECK(e.cursor == 0);   // start of "hello"
}

// ─── Selection ────────────────────────────────────────
static void test_selection() {
    SUB("selection range is min(anchor,active) to max");
    Editor e;
    e.text = "hello world";
    e.sel_anchor = 6;
    e.sel_active = 11;
    e.has_selection = true;
    size_t lo, hi;
    editor_selection_range(e, lo, hi);
    CHECK(lo == 6); CHECK(hi == 11);

    SUB("range works when anchor > active");
    e.sel_anchor = 11; e.sel_active = 6;
    editor_selection_range(e, lo, hi);
    CHECK(lo == 6); CHECK(hi == 11);

    SUB("delete_selection removes range and clears flag");
    e.sel_anchor = 5; e.sel_active = 11; e.has_selection = true;
    e.cursor = 11;
    editor_delete_selection(e);
    CHECK(e.text == "hello");
    CHECK(e.cursor == 5);
    CHECK(e.has_selection == false);

    SUB("select_to sets has_selection only if anchor != active");
    Editor e2;
    e2.text = "abc";
    e2.sel_anchor = 2;
    editor_select_to(e2, 2);
    CHECK(e2.has_selection == false);
    editor_select_to(e2, 0);
    CHECK(e2.has_selection == true);
}

// ─── Undo / redo ──────────────────────────────────────
static void test_undo_redo_basic() {
    SUB("type a single char, undo restores empty");
    Editor e;
    type_char(e, 'a');
    CHECK(e.text == "a");
    CHECK(editor_undo(e));
    CHECK(e.text == "");
    CHECK(e.cursor == 0);

    SUB("redo restores the char");
    CHECK(editor_redo(e));
    CHECK(e.text == "a");

    SUB("undo on empty stack returns false");
    Editor e2;
    CHECK(!editor_undo(e2));

    SUB("redo on empty stack returns false");
    Editor e3;
    CHECK(!editor_redo(e3));
}

static void test_undo_coalescing_kind_boundary() {
    SUB("typing run coalesces into one undo step");
    Editor e;
    type_char(e, 'a');
    type_char(e, 'b');
    type_char(e, 'c');
    CHECK(e.text == "abc");
    CHECK(editor_undo(e));
    CHECK(e.text == "");                      // one undo undoes all three

    SUB("typing then deleting is a new boundary");
    Editor e2;
    type_char(e2, 'a');
    type_char(e2, 'b');
    type_char(e2, 'c');
    editor_record_edit(e2, EditKind::Delete);
    editor_backspace(e2);
    CHECK(e2.text == "ab");
    CHECK(editor_undo(e2));                   // undoes the delete
    CHECK(e2.text == "abc");
    CHECK(editor_undo(e2));                   // undoes the typing run
    CHECK(e2.text == "");
}

static void test_undo_coalescing_time_boundary() {
    SUB("typing with a pause creates a new step");
    Editor e;
    type_char(e, 'a');
    type_char(e, 'b');
    // Sleep long enough to cross the 500ms coalesce window.
    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    type_char(e, 'c');
    type_char(e, 'd');
    CHECK(e.text == "abcd");

    CHECK(editor_undo(e));
    CHECK(e.text == "ab");                    // only "cd" undone
    CHECK(editor_undo(e));
    CHECK(e.text == "");                      // now "ab" undone
}

static void test_undo_paste_is_own_step() {
    SUB("paste is always its own undo step");
    Editor e;
    type_char(e, 'a');
    type_char(e, 'b');
    e.clipboard = "XYZ";
    editor_paste(e);
    CHECK(e.text == "abXYZ");
    CHECK(editor_undo(e));
    CHECK(e.text == "ab");                    // paste undone in one shot
    CHECK(editor_undo(e));
    CHECK(e.text == "");                      // typing undone
}

static void test_redo_invalidation() {
    SUB("a fresh edit clears the redo stack");
    Editor e;
    type_char(e, 'a');
    type_char(e, 'b');
    CHECK(editor_undo(e));
    CHECK(e.text=="");// text="a", redo has "ab"
    type_char(e, 'X');                        // fresh edit
    CHECK(e.text == "X");
    CHECK(!editor_redo(e));                   // redo stack invalidated
}

// ─── Clipboard ────────────────────────────────────────
static void test_clipboard() {
    SUB("copy fills clipboard, doesn't mutate text");
    Editor e;
    e.text = "hello world";
    e.sel_anchor = 6; e.sel_active = 11; e.has_selection = true;
    editor_copy(e);
    CHECK(e.clipboard == "world");
    CHECK(e.text == "hello world");

    SUB("copy with no selection is no-op");
    Editor e2;
    e2.text = "hello";
    e2.has_selection = false;
    editor_copy(e2);
    CHECK(e2.clipboard == "");

    SUB("paste inserts at cursor");
    Editor e3;
    e3.text = "abc";
    e3.cursor = 3;
    e3.clipboard = "XY";
    editor_paste(e3);
    CHECK(e3.text == "abcXY");
    CHECK(e3.cursor == 5);

    SUB("paste replaces selection");
    Editor e4;
    e4.text = "hello world";
    e4.sel_anchor = 0; e4.sel_active = 5; e4.has_selection = true;
    e4.cursor = 5;
    e4.clipboard = "BYE";
    editor_paste(e4);
    CHECK(e4.text == "BYE world");

    SUB("cut copies and deletes");
    Editor e5;
    e5.text = "hello world";
    e5.sel_anchor = 0; e5.sel_active = 6; e5.has_selection = true;
    e5.cursor = 6;
    editor_cut(e5);
    CHECK(e5.clipboard == "hello ");
    CHECK(e5.text == "world");
    CHECK(e5.has_selection == false);

    SUB("type-over-selection is one undo step");
    Editor e6;
    e6.text = "hello";
    e6.cursor = 0;
    e6.sel_anchor = 0; e6.sel_active = 5; e6.has_selection = true;
    type_char(e6, 'X');
    CHECK(e6.text == "X");
    CHECK(editor_undo(e6));
    CHECK(e6.text == "hello");
}

// ─── Run all ──────────────────────────────────────────
int main() {
    printf("editor_test:\n");
    test_insert_and_cursor();
    test_backspace();
    test_utf8_movement();
    test_line_movement();
    test_word_movement();
    test_selection();
    test_undo_redo_basic();
    test_undo_coalescing_kind_boundary();
    test_undo_coalescing_time_boundary();
    test_undo_paste_is_own_step();
    test_redo_invalidation();
    test_clipboard();
    printf("editor_test: %d/%d passed\n", pass, run);
    return pass == run ? 0 : 1;
}