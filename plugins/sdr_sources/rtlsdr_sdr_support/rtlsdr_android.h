#pragma once
#ifdef __ANDROID__
#include <string>
#include <atomic>
#include <vector>

namespace rtl_android
{
    struct Device { std::string path, name; int index; };
    std::vector<Device> devices();
    int index(const std::string &path);
    // Returns false while the Android permission dialog is pending; throws on failure.
    bool permission(const std::string &path, bool request);
    // Only for callers already on a worker (the NDSP flowgraph).
    void wait_permission(const std::string &path, const std::atomic<bool> &cancelled);
}
#endif
