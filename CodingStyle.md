# Coding style guidelines

While most software developers have a strong opinion about coding 
style and conventions, the general agreement is that having a single 
coding style in your project is beneficial for the readability and 
maintainability. 

Coding style of this project is enforced through `.clang-format`. 
Most IDE's nowadays pick up the clang-format file automatically. If
this is not the case, please run it manually before committing code.

Note that different IDE's are compiled against different versions of 
clang-format. This clang-format file is created in such a way, that 
it should work with most IDE's out-of-the-box, and can apply changes 
when the file is saved.

There may be violations of these guidelines in the code, due to
historical reasons or, in rare instances, other considerations.
We intend to fix such violations; patches that correct them
are most welcome - but should be tested carefully across the
supported compilation environments (Arduino and platformio).

## Guidelines 

A few guidelines need to be taken into consideration while using 
clang-format:

1. Include order and `".."` vs `<...>` matters. Clang-format 
   automatically re-orders your include files. This configuration 
   is created in such a way that header file includes always add
   the minimum number of implicit dependencies. If this generates 
   problems, you should be fixing your includes, instead of disabling
   clang-format.
2. Preprocessor commands are not formatted nicely in some cases.
   This can hurt readibility in rare cases, in which case it's 
   okay to disable clang-format temporarily with 
   `// clang-format off` and `// clang-format on`. Most notably, 
   machine definitions should have clang-format off.
3. Use `#pragma once` in all header files. The reason is that 
   preprocessor `#ifdef`s are nested automatically, which making 
   things messy when using the alternative.

## Classes and namespaces

Filenames should correspond with clas names, folder names should 
correspond with namespace names. This implies that a file should 
have a single class.

## Naming

- Class names and namespace names are named `CamelCase`. Note that 
  class and namespace names should only start with an `_` if they are 
  (A) not in the global namespace and (b) should otherwise start with a digit.
  For example `_10V`.
- Class member functions should be `snake_case`
- Class member variables should be `_snake_case` with a leading `_`.
 
Namespaces should have classes, and classes should have members. Avoid
using functions that have no class attached to them.

## Using namespace

- `using namespace` is not allowed in header files, except when 
  using it in the body of a function.
- Try to be conservative with `using namespace` in cpp files. 
  Prefer to use it in a function body whenever possible for clarity
  what is used where.

## Including files

- It's a good practice to include just what you need. In general,
  try to include as much as possible in the cpp file, and as little 
  as possible in the header file.
- A CPP file should normally include the corresponding header file 
  first (e.g. `WebSettings.cpp` should include `WebSettings.h`)
  and then everything else.
- When including a system or library file, use `<...>` syntax; 
  when including a local file, use `"..."` syntax. Some IDE's
  actually have trouble compiling if not done correctly.
- Never include a cpp file; always header files!
