// The app's own header. src/main.c includes it together with the library's core.h, which shows
// why a project needs both its own -I<project>/include/ and the include path of what it links.
#ifndef REPORT_H_
#define REPORT_H_

void report_value(int value, int square, int cube);
void report_library(const char* version);

#endif // REPORT_H_
