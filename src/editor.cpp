#include "editor.h"
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>

static constexpr size_t AXL_HEADER_LEN = 16;
static const unsigned char AXL_MAGIC[5] = {'A', 'X', 'L', '\0','\n'};

void editor_ensure_data_dir(const Editor& e) {
    size_t slash = e.path.find_last_of('/');
    if (slash == std::string::npos) return;
    std::string dir = e.path.substr(0, slash);
    mkdir(dir.c_str(), 0755);   // ignore EEXIST
}

void editor_set_status(Editor& e, const char* msg){
    e.status = msg;
    e.status_set = std::chrono::steady_clock::now();
}

const char* editor_get_status(const Editor& e, double max_age_seconds){
    if(e.status.empty()) return nullptr;
    auto now = std::chrono::steady_clock::now();
    double age = std::chrono::duration<double>(now - e.status_set).count();
    if(age > max_age_seconds) return nullptr;
    return e.status.c_str();
}
void editor_insert_utf8(Editor& e, const char* bytes, int n){
    if (n <= 0) return;
    if (n == 1) {
        unsigned char c = (unsigned char)bytes[0];
        if (c < 0x20 && c != '\t' && c != '\n') return;   // <-- allow newline
        if (c == 0x7F) return;
    }

    e.last_input = std::chrono::steady_clock::now();
    e.measure_pending = true;
    e.text.insert(e.cursor, bytes, (size_t)n);
    e.cursor += (size_t)n;
    e.dirty = true;
    e.modified = true;
};



static size_t utf8_prev(const std::string& s, size_t pos){
    if (pos == 0) return 0;
    do {pos--;} while (pos > 0 && ((unsigned char)s[pos] & 0xC0) == 0x80);
    return pos;
    
}

static size_t utf8_next(const std::string& s, size_t pos){
    if(pos >= s.size()) return s.size();
    pos++;
    while(pos < s.size() && ((unsigned char)s[pos] & 0xC0) == 0x80) pos++;
    return pos;
}


static size_t line_start(const std::string& s, size_t pos) {
    while (pos > 0 && s[pos - 1] != '\n') pos--;
    return pos;
}

static size_t line_end(const std::string& s, size_t pos) {
    while (pos < s.size() && s[pos] != '\n') pos++;
    return pos;
}

// --- Mutations now operate at cursor ---



void editor_backspace(Editor& e) {
    if (e.cursor > e.text.size()) e.cursor = e.text.size();

    if (e.cursor == 0) return;
    e.last_input      = std::chrono::steady_clock::now();
    e.measure_pending = true;
    size_t prev = utf8_prev(e.text, e.cursor);
    e.text.erase(prev, e.cursor - prev);
    e.cursor = prev;
    e.dirty    = true;
    e.modified = true;
}

void editor_newline(Editor& e) {
    editor_insert_utf8(e, "\n", 1);
}

// --- Cursor movement (no buffer change, only cursor + dirty) ---

void editor_move_left(Editor& e) {
    if (e.cursor > e.text.size()) e.cursor = e.text.size();

    if (e.cursor == 0) return;
    e.cursor = utf8_prev(e.text, e.cursor);
    e.dirty = true;
}

void editor_move_right(Editor& e) {
    if (e.cursor > e.text.size()) e.cursor = e.text.size();

    if (e.cursor >= e.text.size()) return;
    e.cursor = utf8_next(e.text, e.cursor);
    e.dirty = true;
}

void editor_move_home(Editor& e) {
    if (e.cursor > e.text.size()) e.cursor = e.text.size();

    size_t ls = line_start(e.text, e.cursor);
    if (e.cursor != ls) { e.cursor = ls; e.dirty = true; }
}

void editor_move_end(Editor& e) {
    if (e.cursor > e.text.size()) e.cursor = e.text.size();

    size_t le = line_end(e.text, e.cursor);
    if (e.cursor != le) { e.cursor = le; e.dirty = true; }
}

void editor_move_up(Editor& e) {
    if (e.cursor > e.text.size()) e.cursor = e.text.size();

    size_t ls  = line_start(e.text, e.cursor);
    if (ls == 0) return;                         // already on first line
    size_t col = e.cursor - ls;                  // byte column on current line
    size_t prev_end   = ls - 1;                  // points at the \n separating lines
    size_t prev_start = line_start(e.text, prev_end);
    size_t prev_len   = prev_end - prev_start;
    e.cursor = prev_start + (col < prev_len ? col : prev_len);
    e.dirty = true;
}

void editor_move_down(Editor& e) {
    if (e.cursor > e.text.size()) e.cursor = e.text.size();

    size_t le = line_end(e.text, e.cursor);
    if (le == e.text.size()) return;             // already on last line
    size_t ls   = line_start(e.text, e.cursor);
    size_t col  = e.cursor - ls;
    size_t next_start = le + 1;                  // skip the \n
    size_t next_end   = line_end(e.text, next_start);
    size_t next_len   = next_end - next_start;
    e.cursor = next_start + (col < next_len ? col : next_len);
    e.dirty = true;
}

static int char_class(unsigned char c) {
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') return 0;
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
        (c >= '0' && c <= '9') || c == '_' || c >= 0x80) return 1;
    return 2;
}

static size_t word_right(const std::string& s, size_t pos){
    size_t n = s.size();
    if (pos >= n) return n;

    while (pos < n && char_class((unsigned char)s[pos]) == 0) pos = utf8_next(s, pos);
    if (pos >= n) return n;

    int cls = char_class((unsigned char)s[pos]);
    while (pos < n && char_class((unsigned char)s[pos]) == cls)
        pos = utf8_next(s, pos);
 
    return pos;
}

static size_t word_left(const std::string& s, size_t pos) {
    if (pos == 0) return 0;
 
    // Step back over whitespace.
    size_t p = utf8_prev(s, pos);
    while (p > 0 && char_class((unsigned char)s[p]) == 0)
        p = utf8_prev(s, p);
 
    // If we're at the start, done.
    if (p == 0 && char_class((unsigned char)s[p]) == 0) return 0;
 
    // Skip the run of this class going left. Stop at the first char of it.
    int cls = char_class((unsigned char)s[p]);
    while (p > 0) {
        size_t prev = utf8_prev(s, p);
        if (char_class((unsigned char)s[prev]) != cls) break;
        p = prev;
    }
    return p;
}


void editor_move_word_left(Editor& e){
    if (e.cursor > e.text.size()) e.cursor = e.text.size();
    if (e.cursor == 0) return;

    e.cursor = word_left(e.text, e.cursor);
    e.dirty = true;
}

void editor_move_word_right(Editor& e){
    if (e.cursor > e.text.size()) e.cursor = e.text.size();
    if (e.cursor >= e.text.size()) return;

    e.cursor = word_right(e.text, e.cursor);
    e.dirty = true;
}

void editor_delete_word_left(Editor& e){
    if (e.cursor > e.text.size()) e.cursor = e.text.size();
    if (e.cursor == 0) return;
    size_t target = word_left(e.text, e.cursor);
    if(target >= e.cursor) return;
    e.last_input = std::chrono::steady_clock::now();
    e.measure_pending = true;
    e.text.erase(target, e.cursor - target);
    e.cursor = target;
    e.dirty = true;
    e.modified = true;
}



void editor_clear_selection(Editor& e) {
    if (e.has_selection) {
        e.has_selection = false;
        e.dirty = true;
    }
}

void editor_select_to(Editor& e, size_t new_cursor) {
    e.sel_active = new_cursor;
    e.has_selection = (e.sel_anchor != e.sel_active);
    e.dirty = true;
}

void editor_selection_range(const Editor& e, size_t& lo, size_t& hi) {
    if (e.sel_anchor <= e.sel_active) {
        lo = e.sel_anchor;
        hi = e.sel_active;
    } else {
        lo = e.sel_active;
        hi = e.sel_anchor;
    }
}

void editor_delete_selection(Editor& e) {
    if (!e.has_selection) return;
    size_t lo, hi;
    editor_selection_range(e, lo, hi);
    if (hi > e.text.size()) hi = e.text.size();
    if (lo >= hi) {
        e.has_selection = false;
        return;
    }
    e.last_input = std::chrono::steady_clock::now();
    e.measure_pending = true;
    e.text.erase(lo, hi - lo);
    e.cursor = lo;
    e.has_selection = false;
    e.dirty = true;
    e.modified = true;
}




static size_t line_index_of_cursor(const std::string& s, size_t cursor){
    size_t lines = 0;
    if (cursor > s.size()) cursor = s.size();
    for (size_t i = 0; i < cursor; i++){
        if(s[i] == '\n') lines++;
    }
    return lines;
}

void editor_scroll_to_cursor(Editor& e,
                              float viewport_top_px,
                              float viewport_height_px,
                              float line_height_px) {
    if (line_height_px <= 0.0f) return;

    size_t cursor_line = line_index_of_cursor(e.text, e.cursor);
    float cursor_y = (float)(cursor_line + 1) * line_height_px;

    // Keep one line of margin so the cursor doesn't sit flush against the edge.
    float margin = line_height_px;

    float cursor_screen_y = cursor_y - e.scroll_y;
    if (cursor_screen_y < margin) {
        e.scroll_y = cursor_y - margin;
        e.dirty = true;
    } else if (cursor_screen_y > viewport_height_px - margin) {
        e.scroll_y = cursor_y - (viewport_height_px - margin);
        e.dirty = true;
    }

    if (e.scroll_y < 0.0f) {
        e.scroll_y = 0.0f;
    }
    (void)viewport_top_px;  // reserved for future per-panel viewports
}

void editor_scroll_lines(Editor& e, int n_lines, float line_height_px,
                         float viewport_top_px, float viewport_height_px,
                         float text_total_height_px) {
    (void)viewport_top_px;
    (void)viewport_height_px;
    if (line_height_px <= 0.0f || n_lines == 0) return;

    e.scroll_y += (float)n_lines * line_height_px;

    float max_scroll = text_total_height_px - line_height_px;
    if (max_scroll < 0.0f) max_scroll = 0.0f;
    if (e.scroll_y < 0.0f)        e.scroll_y = 0.0f;
    if (e.scroll_y > max_scroll)  e.scroll_y = max_scroll;
    e.dirty = true;
}


void editor_page_up(Editor& e, float viewport_height_px, float line_height_px) {
    if (line_height_px <= 0.0f) return;
    int lines_per_page = (int)(viewport_height_px / line_height_px);
    if (lines_per_page < 1) lines_per_page = 1;
    for (int i = 0; i < lines_per_page; i++) {
        editor_move_up(e);
    }
}

void editor_page_down(Editor& e, float viewport_height_px, float line_height_px) {
    if (line_height_px <= 0.0f) return;
    int lines_per_page = (int)(viewport_height_px / line_height_px);
    if (lines_per_page < 1) lines_per_page = 1;
    for (int i = 0; i < lines_per_page; i++) {
        editor_move_down(e);
    }
}



bool editor_save(Editor &e){
    if(e.path.empty()){
        editor_set_status(e, "save failed: no path");
        return false;
    }

    std::string tmp = e.path + ".tmp";

    int fd = open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        char buf[128];
        snprintf(buf, sizeof(buf), "save failed: %s", strerror(errno));
        editor_set_status(e,buf);
        return false;
    }

    ssize_t wrote;

    if (e.path.size() >= 4 && e.path.compare(e.path.size() - 4, 4, ".axl") == 0){//(path_is_axl(e.path)) {
        unsigned char header[AXL_HEADER_LEN] = {0};
        memcpy(header, AXL_MAGIC, 4);
        header[4] = 1;   // format version
        header[5] = 0;   // flags
    
        wrote = write(fd, header, AXL_HEADER_LEN);
        if (wrote != (ssize_t)AXL_HEADER_LEN) goto write_err;
    }


    if(!e.text.empty()){
        wrote = write(fd, e.text.data(), e.text.size());
        if(wrote != (ssize_t)e.text.size()) goto write_err;
    }

    if(fsync(fd) != 0) goto write_err;
    if(close(fd) != 0) {
        char buf[128];
        snprintf(buf, sizeof(buf), "save failed on close: %s", strerror(errno));
        editor_set_status(e, buf);
        unlink(tmp.c_str());
        return false;
    }

    if(rename(tmp.c_str(), e.path.c_str()) != 0){
        char buf[128];
        snprintf(buf, sizeof(buf), "save failed on rename: %s", strerror(errno));
        editor_set_status(e, buf);
        unlink(tmp.c_str());
        return false;
    }

    e.modified = false;
    char buf[128];
    snprintf(buf, sizeof(buf), "saved %s (%zu bytes)", e.path.c_str(), e.text.size());
    editor_set_status(e, buf);
    return true;

write_err:
    {
        char errbuf[128];
        snprintf(errbuf, sizeof(errbuf), "save failed: %s", strerror(errno));
        editor_set_status(e, errbuf);
    }
    close(fd);
    unlink(tmp.c_str());
    return false;
}

bool editor_load(Editor& e) {
    int fd = open(e.path.c_str(), O_RDONLY);
    if (fd < 0) {
        char buf[128];
        snprintf(buf, sizeof(buf), "load failed: %s", strerror(errno));
        editor_set_status(e, buf);
        return false;
    }

    struct stat st;
    if (fstat(fd, &st) != 0) {
        editor_set_status(e, "load failed: stat");
        close(fd);
        return false;
    }

    // Size sanity — refuse to load >64MB into a flat string for now.
    if (st.st_size > 64 * 1024 * 1024) {
        editor_set_status(e, "load failed: file >64MB");
        close(fd);
        return false;
    }

    std::string contents;
    contents.resize((size_t)st.st_size);
    ssize_t got = read(fd, contents.data(), (size_t)st.st_size);
    close(fd);
    if (got != st.st_size) {
        editor_set_status(e, "load failed: short read");
        return false;
    }

    // Detect AXL header. If present, skip it; otherwise load as plain text.
    bool has_header = (contents.size() >= AXL_HEADER_LEN) &&
                      (memcmp(contents.data(), AXL_MAGIC, 4) == 0);

    if (has_header) {
        e.text.assign(contents.begin() + AXL_HEADER_LEN, contents.end());
    } else {
        e.text = std::move(contents);
    }

    e.dirty    = true;
    e.modified = false;

    char buf[128];
    snprintf(buf, sizeof(buf), "loaded %s (%zu bytes%s)",
             e.path.c_str(), e.text.size(),
             has_header ? ", .axl" : ", plain text");
    e.cursor = 0;
    editor_set_status(e, buf);
    return true;
}

