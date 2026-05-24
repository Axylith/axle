# Changelog

All notable changes to Axylith are documented here.

Format based on [Keep a Changelog](https://keepachangelog.com/).

---

## [Unreleased]

### Added
- Text editor core (`editor.h` / `editor.cpp`): UTF-8 buffer, cursor model,
  dirty/modified flags, status messages, input-latency instrumentation hooks
- Atomic save/load to the `.axl` v1 format (16-byte header + raw UTF-8),
  with plain-text fallback for any non-AXL file
- Cursor movement: left/right (UTF-8 codepoint aware), up/down (byte-column
  preserving), Home/End (line-relative)
- Multi-line text rendering: `append_text_run` composes runs without resetting
  glyph_count; `\n` advances the baseline by one line height
- `build_text_vertices_with_cursor` -- records the cursor's screen position
  during layout so the cursor quad can be drawn at the correct offset
- Scrolling viewport: `Editor.scroll_y` pixel offset, `editor_scroll_to_cursor`
  (keeps cursor visible with a one-line margin), `editor_scroll_lines`
  (mouse-wheel / clamped), `editor_page_up` / `editor_page_down`
- Mouse-wheel scrolling via X11 Button4/Button5; PageUp/PageDown keybinds
- XIM/XIC input path for correct UTF-8 input (Shift, AltGr, dead keys, multibyte)
- On-screen HUD (`metrics.h`): EMA-smoothed frame time, keystroke-to-submit
  input latency, glyph count; F1 toggles visibility
- `exe_relative()` path resolution -- shaders and assets load correctly from any
  working directory via `/proc/self/exe`
- Documentation set: `docs/FORMAT.md` (the `.axl` format spec, v0/v1/v2),
  `docs/API.md` (public C++ API across all subsystems), `docs/introduction.mdx`
  and `docs/quickstart.mdx` (Mintlify pages)

### Changed
- Editor mutation functions (`editor_insert_utf8`, `editor_backspace`,
  `editor_newline`) now operate at the cursor position rather than at the
  end of the buffer
- `editor_load` resets the cursor to 0 after replacing the buffer
- README rewritten -- honest pre-V1 status section, tightened badge set,
  removed unverified claims and the decorative "Powered By" wall
- CI installs now bypass the unreliable Azure Ubuntu mirror and cap apt
  timeouts (prevents multi-minute hangs)

### Fixed
- `decode_delta` operator-precedence bug: the cast bound tighter than the
  shift, so every decoded timestamp collapsed to tier 0. Caught by the
  "Tier 1: hours" format test.
- `utf8_next` used `if` instead of `while`, so it only skipped one
  continuation byte -- broke navigation over 3- and 4-byte codepoints
- Newline (0x0A) was rejected by the control-byte filter in
  `editor_insert_utf8`, so `editor_newline` silently inserted nothing
- `editor_save` used `memccpy` instead of `memcpy`, passing a pointer cast
  to `size_t` as the length
- `load_spirv` returned null for missing files, leading to a null shader
  module passed to Vulkan; now checks the SPIR-V magic and short reads
- Various build warnings: `#pragma pack(pop)` trailing semicolon, missing
  explicit casts in `types.h`, internal-linkage `static const char*` in a
  header

### Notes
- The format test suite is currently 22 tests (the count in earlier entries
  was from before the suite was trimmed and the decode bug was fixed)

---

## [0.1.0-pre] -- Initial scaffold

### Added
- X11 window creation with WM_DELETE_WINDOW support
- Vulkan 1.3 initialization (instance, surface, device, swapchain)
- Dynamic rendering with vkCmdBeginRendering/vkCmdEndRendering
- Graphics pipeline with vertex + fragment shaders
- First colored quad rendering (Tol palette teal)
- Vertex buffer with GPU memory allocation
- Alpha blending in pipeline
- Threaded Vulkan initialization (window at 0.3ms)
- Resource monitoring (CPU/RAM CSV output)
- X connection error handling (clean shutdown on WM kill)
- .axl binary format: Block (12B), Page (22B), Project (10B), Header (32B)
- Tiered timestamp encoding (minutes/hours/days in 2 bytes)
- MTSDF text rendering pipeline with JetBrains Mono atlas
- CI/CD: GitLab CI + GitHub Actions + CircleCI
- Sanitizer builds (ASan + UBSan)
- Static analysis (clang-tidy)
- Dual compiler builds (GCC + Clang)
- SonarCloud integration
- AGPL v3 license
- CONTRIBUTING.md with CLA and mentorship program
- CODE_OF_CONDUCT.md with AI policy and three-strike system
- SECURITY.md
- GOVERNANCE.md
- FOUNDERS.md (template)
- funding.json and FUNDING.yml
- README.md with badges