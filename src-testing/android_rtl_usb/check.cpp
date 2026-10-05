#include "rtlsdr_android.h"
#include <android_native_app_glue.h>
#include <libusb.h>
#include <cassert>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <iostream>
#include <stdexcept>
#include <atomic>
#include <thread>
#include <chrono>

struct libusb_context {};
struct libusb_device { uint16_t vid, pid; uint8_t bus, address; };
static libusb_context context;
static libusb_device unrelated{0x1234, 0x5678, 1, 1}, rtl{0x0bda, 0x2838, 1, 7}, second{0x0bda, 0x2832, 2, 9};
static libusb_device *usb_list[] = {&unrelated, &rtl, &second, nullptr};
static int usb_count = 3, init_error = 0, exits = 0, freed = 0;
extern "C" {
int libusb_init(libusb_context **ctx) { *ctx = &context; return init_error; }
void libusb_exit(libusb_context *) { ++exits; }
ssize_t libusb_get_device_list(libusb_context *, libusb_device ***list) { *list = usb_list; return usb_count; }
void libusb_free_device_list(libusb_device **, int unref) { assert(unref == 1); ++freed; }
int libusb_get_device_descriptor(libusb_device *dev, libusb_device_descriptor *desc) { desc->idVendor = dev->vid; desc->idProduct = dev->pid; return 0; }
uint8_t libusb_get_bus_number(libusb_device *dev) { return dev->bus; }
uint8_t libusb_get_device_address(libusb_device *dev) { return dev->address; }
// No libusb_open implementation: enumeration must link and run without it.
}

static std::atomic<int> permission_state{0}, requests{0};
static int frames = 0, attaches = 0, detaches = 0;
static bool jni_detached = false, last_request = false, jni_exception = false;
static jint push(JNIEnv *, jint) { ++frames; return JNI_OK; }
static jobject pop(JNIEnv *, jobject) { --frames; return nullptr; }
static jclass object_class(JNIEnv *, jobject) { return reinterpret_cast<jclass>(1); }
static jmethodID method(JNIEnv *, jclass, const char *name, const char *signature) {
    assert(std::strcmp(name, "rtlUsbPermission") == 0);
    assert(std::strcmp(signature, "(Ljava/lang/String;Z)I") == 0);
    return reinterpret_cast<jmethodID>(1);
}
static jstring string(JNIEnv *, const char *path) {
    assert(std::strcmp(path, "/dev/bus/usb/001/007") == 0);
    return reinterpret_cast<jstring>(1);
}
static jint call(JNIEnv *, jobject, jmethodID, va_list args) {
    (void)va_arg(args, jstring);
    last_request = va_arg(args, int);
    requests += last_request;
    return permission_state;
}
static jboolean exception(JNIEnv *) { return jni_exception; }
static void clear(JNIEnv *) { jni_exception = false; }
static JNIEnv env;
static jint get_env(JavaVM *, void **out, jint) { *out = &env; return jni_detached ? JNI_EDETACHED : JNI_OK; }
static jint attach(JavaVM *, JNIEnv **out, void *) { *out = &env; ++attaches; return JNI_OK; }
static jint detach(JavaVM *) { ++detaches; return JNI_OK; }

template<typename F> void fails(F f, const char *message) {
    try { f(); assert(false && "Expected an error"); }
    catch (const std::runtime_error &e) { assert(std::strstr(e.what(), message)); }
}

int main() {
    auto devices = rtl_android::devices();
    assert(devices.size() == 2);
    assert(devices[0].name == "Generic RTL2832U OEM #0");
    assert(devices[0].path == "/dev/bus/usb/001/007");
    assert(devices[1].index == 1);
    assert(rtl_android::index(devices[1].path) == 1);
    usb_list[0] = &second; usb_list[1] = &unrelated; usb_list[2] = &rtl;
    assert(rtl_android::index(devices[0].path) == 1); // Identity survives enumeration reordering.
    usb_count = 0;
    assert(rtl_android::devices().empty());
    fails([&] { rtl_android::index(devices[0].path); }, "not connected");
    usb_count = -4;
    fails([] { rtl_android::devices(); }, "Could not list");
    init_error = -3;
    fails([] { rtl_android::devices(); }, "Could not enumerate");
    assert(exits == 6 && freed == 5);

    JNINativeInterface functions{};
    functions.PushLocalFrame = push; functions.PopLocalFrame = pop;
    functions.GetObjectClass = object_class; functions.GetMethodID = method;
    functions.NewStringUTF = string; functions.CallIntMethodV = call;
    functions.ExceptionCheck = exception; functions.ExceptionClear = clear;
    env.functions = &functions;
    JNIInvokeInterface vm_functions{};
    vm_functions.GetEnv = get_env; vm_functions.AttachCurrentThread = attach; vm_functions.DetachCurrentThread = detach;
    JavaVM vm{&vm_functions};
    ANativeActivity activity{&vm, reinterpret_cast<jobject>(1)};
    android_app app{&activity};
    char pointer[64]; std::snprintf(pointer, sizeof(pointer), "%llu", (unsigned long long)&app);
    setenv("LIBUSB_ANDROID_JVM_PTR", pointer, 1);
    assert(!rtl_android::permission(devices[0].path, true));
    assert(last_request);
    assert(!rtl_android::permission(devices[0].path, false));
    assert(!last_request);
    permission_state = 1;
    assert(rtl_android::permission(devices[0].path, false));
    permission_state = -1;
    fails([&] { rtl_android::permission(devices[0].path, false); }, "was denied");
    permission_state = -2;
    fails([&] { rtl_android::permission(devices[0].path, false); }, "disconnected");
    permission_state = -3;
    fails([&] { rtl_android::permission(devices[0].path, false); }, "Could not request");
    jni_exception = true;
    fails([&] { rtl_android::permission(devices[0].path, false); }, "bridge is unavailable");
    assert(!jni_exception && frames == 0);
    jni_detached = true; permission_state = 1;
    assert(rtl_android::permission(devices[0].path, false));
    assert(attaches == 1 && detaches == 1 && frames == 0);
    // The NDSP caller waits on a worker and opens automatically after a grant.
    permission_state = 0; requests = 0;
    std::atomic<bool> cancelled{false}, completed{false};
    std::thread waiting([&] { rtl_android::wait_permission(devices[0].path, cancelled); completed = true; });
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (requests == 0) { assert(std::chrono::steady_clock::now() < deadline); std::this_thread::yield(); }
    assert(!completed);
    permission_state = 1; waiting.join();
    assert(completed && requests == 1);
    permission_state = 0; requests = 0; completed = false;
    std::thread cancelling([&] {
        fails([&] { rtl_android::wait_permission(devices[0].path, cancelled); }, "was cancelled");
        completed = true;
    });
    deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (requests == 0) { assert(std::chrono::steady_clock::now() < deadline); std::this_thread::yield(); }
    cancelled = true; cancelling.join();
    assert(completed && frames == 0);
    unsetenv("LIBUSB_ANDROID_JVM_PTR");
    fails([&] { rtl_android::permission(devices[0].path, true); }, "activity is unavailable");
    std::cout << "Android RTL USB enumeration and permission regression checks passed\n";
}
