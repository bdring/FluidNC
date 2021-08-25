#pragma once

// Usage:
//
// EnumItem stepTypes[] = {
// { ST_TIMED, "Timed" }, { ST_RMT, "RMT" }, { ST_I2S_STATIC, "I2S_static" }, { ST_I2S_STREAM, "I2S_stream" }, EnumItem(ST_RMT)
// };

struct EnumItem {
    // Used for brace initialization
    EnumItem() {} 

    // Set enumItem with a default value as last item in the EnumItem array. This is the terminator.
    EnumItem(int defaultValue) : value(defaultValue), name(nullptr) {}

    // Other items are here.
    EnumItem(int val, const char* n) : value(val), name(n) {}

    int         value;
    const char* name;
};
