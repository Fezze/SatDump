#pragma once
// Only the fields used by the permission bridge; this harness runs on the host.
#include <jni.h>
struct ANativeActivity { JavaVM *vm; jobject clazz; };
struct android_app { ANativeActivity *activity; };
