// Public header of the demo shared library. The build script adds <project>/include/ with -I, so
// a header is never passed to the compiler as an input.
#ifndef CORE_H_
#define CORE_H_

int core_square(int value);
int core_cube(int value);
const char* core_version(void);

#endif // CORE_H_
