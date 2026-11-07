// SPDX-License-Identifier: MIT
// Description of one editable setting. The pages build their controls from tables of these.
#pragma once
#include <cstddef>

namespace app {

enum class SType { Bool, Int, Float, Enum, Text, Buttons };

struct Setting {
    const char* section;
    const char* key;
    SType       type;
    const char* label;
    const char* tooltip;   // nullptr = use the comment above the key in the default INI text
    double      min, max, step;   // Int / Float; max <= min means no limit
    const char* values;    // Enum: INI values separated by '|', shown as they are
    const char* group;     // heading; a new heading starts a new block of rows
    const char* part;      // picture part this setting belongs to (highlighted when focused)
    bool        slider;    // Int / Float: slider + edit
};

#define ST_BOOL(sec, key, label, group, part)                    {sec, key, SType::Bool, label, nullptr, 0, 0, 0, nullptr, group, part, false}
#define ST_INT(sec, key, label, group, part, lo, hi, st)         {sec, key, SType::Int, label, nullptr, lo, hi, st, nullptr, group, part, false}
#define ST_FLOAT(sec, key, label, group, part, lo, hi, st)       {sec, key, SType::Float, label, nullptr, lo, hi, st, nullptr, group, part, false}
#define ST_SLIDER(sec, key, label, group, part, lo, hi, st)      {sec, key, SType::Float, label, nullptr, lo, hi, st, nullptr, group, part, true}
#define ST_ENUM(sec, key, label, group, part, vals)              {sec, key, SType::Enum, label, nullptr, 0, 0, 0, vals, group, part, false}
#define ST_TEXT(sec, key, label, group, part)                    {sec, key, SType::Text, label, nullptr, 0, 0, 0, nullptr, group, part, false}
#define ST_BUTTONS(sec, key, label, group, part)                 {sec, key, SType::Buttons, label, nullptr, 0, 0, 0, nullptr, group, part, false}

} // namespace app
