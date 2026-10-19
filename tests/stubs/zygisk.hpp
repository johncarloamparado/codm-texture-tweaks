#pragma once
#include <jni.h>
namespace zygisk {
enum Option { DLCLOSE_MODULE_LIBRARY = 1 };
struct AppSpecializeArgs { int uid; jstring nice_name; jstring app_data_dir; };
struct Api { void setOption(Option) {} };
class ModuleBase {
public:
    virtual ~ModuleBase() {}
    virtual void onLoad(Api *, JNIEnv *) {}
    virtual void preAppSpecialize(AppSpecializeArgs *) {}
    virtual void postAppSpecialize(const AppSpecializeArgs *) {}
};
}
#define REGISTER_ZYGISK_MODULE(clazz)
