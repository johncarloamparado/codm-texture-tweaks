#pragma once
#include <cstdarg>
enum { ANDROID_LOG_INFO = 4, ANDROID_LOG_WARN = 5 };
extern "C" int __android_log_print(int, const char *, const char *, ...);
extern "C" int __android_log_vprint(int, const char *, const char *, va_list);
