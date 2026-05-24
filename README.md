<div align="center">

# Axylith

A native research environment for code, prose, data, and 3D geometry in one surface.

[Documentation](https://docs.axylith.com) · [File Format](https://docs.axylith.com/format) · [Roadmap](https://github.com/Axylith/axle/blob/development/changelog.md)

[![CI](https://github.com/Axylith/axle/actions/workflows/ci.yml/badge.svg)](https://github.com/Axylith/axle/actions/workflows/ci.yml)
[![Sanitizers](https://github.com/Axylith/axle/actions/workflows/sanitizers.yml/badge.svg)](https://github.com/Axylith/axle/actions/workflows/sanitizers.yml)
[![CodeQL](https://github.com/Axylith/axle/actions/workflows/codeql.yml/badge.svg)](https://github.com/Axylith/axle/actions/workflows/codeql.yml)
[![License: AGPL v3](https://img.shields.io/badge/License-AGPL_v3-blue.svg)](https://www.gnu.org/licenses/agpl-3.0)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://isocpp.org/)
![V1 Progress](.github/badges/v1-progress.svg)
</div>

---

## What this is

Axylith is an early-stage project working toward a single-binary research environment where prose, code, data, and 3D geometry share state and where an integrated AI can reason about all of them. The current build is the substrate: a text editor with multi-line input, cursor movement, save/load to a custom file format, and Vulkan-based text rendering.

The longer-term goal is described in the [roadmap](https://github.com/Axylith/axle/blob/development/changelog.md). That product does not exist yet. This repository is the foundation being built first.

## Status

> **Pre-V1.** The editor is functional for typing, navigating, and saving plain UTF-8 text. The notebook execution, 3D capabilities, and AI integration described in the roadmap have not been implemented.

What works today:

- Multi-line text editor with cursor movement (arrows, Home/End, vertical navigation, word jumping, selection)
- UTF-8 input via X11 input methods, with Shift, AltGr, dead-key, and multibyte handling
- Save and load through a 16-byte-headered `.axl` format; plain-text files load as fallback
- Text rendering through a Vulkan 1.3 MTSDF pipeline (resolution-independent)
- A toggleable on-screen HUD that displays measured frame time and keystroke-to-submit input latency
- Configurable keybindings via a small config file (see `axylith.keys.example`)
- Undo/redo and internal clipboard with Ctrl+Z / Ctrl+Y / Ctrl+C / Ctrl+X / Ctrl+V
- CI matrix across GCC 12, GCC 13, and Clang 17, with AddressSanitizer, UBSan, ThreadSanitizer, and CodeQL

What is on the roadmap but not yet implemented:

- File-open overlay, find/replace, scroll viewport polish, line wrap, syntax highlighting
- Document-as-program execution model (embedded Python interpreter, shared scope across code regions)
- Mesh loading, viewport rendering, and AI-driven geometric operations
- Integrated AI sidebar with structured-output reasoning, not just chat
- Wayland, macOS, and Windows backends (currently Linux/X11 only)

The full roadmap is in the [changelog](https://github.com/Axylith/axle/blob/development/changelog.md).

## Build

Linux only at present. Tested on Ubuntu 24.04, Arch, and Fedora.

```bash
git clone https://github.com/Axylith/axle.git
cd axle
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/axylith
```

Build dependencies (Ubuntu/Debian):

```bash
sudo apt install -y \
    build-essential cmake ninja-build \
    libvulkan-dev vulkan-validationlayers \
    libx11-dev libxext-dev \
    glslc
```

For Arch and Fedora equivalents and a walkthrough of first save/load, see [docs/quickstart](https://docs.axylith.com/quickstart).

## Tests

```bash
cd build
ctest --output-on-failure
```

The current suite covers the on-disk format and the input command layer. Sanitizers run in CI on every push.

## Project constraints

These are the constraints the project tries to hold itself to. They reflect choices about what kind of tool Axylith should be, not claims that they are universally correct.

- **Single native binary.** No bundled browser, no JavaScript runtime, no Electron. The intent is a smaller install footprint and lower memory baseline; the tradeoff is more work per platform and fewer ready-made UI libraries.
- **Minimal external dependencies.** Currently `stb_truetype.h` (header-only) plus system libraries (Vulkan, X11, libc). An embedded Python interpreter will likely be added when the notebook execution layer ships, which will relax this constraint in a documented way.
- **Local-first by default.** Files on disk are the source of truth. No account is required to use any feature. Network features will be opt-in when they exist.
- **Readable file format.** `.axl` files are mostly plain UTF-8 behind a small binary header, and can be opened with `cat` or examined with `od`. Encryption and compression are not in V1.
- **Measured, displayed latency.** Input latency and frame time are shown in the HUD by code in the repository. The intent is for these numbers to be auditable, not asserted.

## File format

`.axl` is a 16-byte header followed by raw UTF-8 content. Files without the header are loaded as plain text. The format is specified in [docs/format](https://docs.axylith.com/format).

```
$ od -An -tx1 -N 16 untitled.axl
 41 58 4c 00  01 00  00 00  00 00 00 00 00 00 00 00
   A  X  L \0   v1     reserved (zeros)
$ tail -c +17 untitled.axl
hello world
```

A V2 structured format (`AXLE` magic, 32-byte header) is specified and unit-tested but not yet emitted by the editor. Readers will support both before writers switch.

## License

Axylith is licensed under the [GNU Affero General Public License v3](LICENSE). A commercial license will be available for organizations that cannot comply with AGPL; for now, please contact `founders@axylith.com` for inquiries.

The companion physics engine repository (when published) will use BSL-1.1 with a planned conversion to Apache-2.0. The full licensing rationale is in the [roadmap](https://github.com/Axylith/axle/blob/development/changelog.md).

## Contributing

This is a small project, currently maintained by one person. Contributions are welcome but the architecture is still in flux, so it may be worth opening an issue before significant work to make sure it fits the direction.

Active development happens on the `development` branch; `main` tracks released versions. Before opening a pull request, please read [CONTRIBUTING.md](CONTRIBUTING.MD) and the [Code of Conduct](CODE_OF_CONDUCT.MD).

First-time contributors: issues labeled `good first issue` or `mentored` are reasonable starting points.

## Acknowledgements

Axylith is built by [Dev Bhatt](https://devbhatt.dev). The project uses font atlas data derived from [JetBrains Mono](https://www.jetbrains.com/lp/mono/) and the MTSDF baking technique described by Viktor Chlumský.

CI infrastructure is provided by [GitHub Actions](https://github.com/features/actions), [GitLab CI](https://about.gitlab.com/), and [CircleCI](https://circleci.com/). Static analysis through [SonarCloud](https://sonarcloud.io/), [DeepSource](https://deepsource.com/), [Snyk](https://snyk.io/), and [CodeQL](https://codeql.github.com/).