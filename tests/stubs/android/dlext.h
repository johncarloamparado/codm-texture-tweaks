#pragma once
struct android_dlextinfo { int flags; };
extern "C" void *android_dlopen_ext(const char *, int, const android_dlextinfo *);
