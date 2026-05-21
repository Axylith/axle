#pragma once

#include <vector>
#include <cstdint>
#include <string>

struct Editor;

using KeySymT = unsigned long;

enum Mods : uint8_t {
    MOD_NONE = 0,
    MOD_CTRL = 1u << 0,
    MOD_SHIFT = 1u << 0,
}

