#pragma once
/* host-test stub: declarations only */
#include <cstdint>
typedef void *jstring;
struct JNIEnv_ {
    const char *GetStringUTFChars(jstring, bool *) { return nullptr; }
    void ReleaseStringUTFChars(jstring, const char *) {}
};
typedef JNIEnv_ JNIEnv;
