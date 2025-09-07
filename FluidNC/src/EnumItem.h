#pragma once

// Usage:
//
// EnumItem fooTypes[] = {
//   { FOO_TYPE0, "Foo Name 0" },
//   { FOO_TYPE1, "Foo Name 1" },
//   { FOO_TYPE2, "Foo Name 2" },
//   EnumItem(FOO_TYPE1)  // Default
// };

struct EnumItem {
    // Used for brace initialization
    EnumItem() {}

    // Set enumItem with a default value as last item in the EnumItem array. This is the terminator.
    explicit EnumItem(int defaultValue) : value(defaultValue), name(nullptr) {}

    // Other items are here.
    EnumItem(int val, const char* n) : value(val), name(n) {}

    int         value;
    const char* name;
};
