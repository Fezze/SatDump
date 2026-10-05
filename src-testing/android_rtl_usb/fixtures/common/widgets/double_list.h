#pragma once
#include <string>
#include <vector>
namespace satdump::widgets {
template<typename T> struct NotatedNum {
    T value;
    NotatedNum(const char *, T v, const char *) : value(v) {}
    T get() { return value; }
    void set(T v) { value = v; }
    bool draw() { return false; }
};
struct DoubleList {
    double value = 1024000;
    DoubleList(const char *) {}
    void set_list(std::vector<double>, bool) {}
    bool set_value(double v, double) { value = v; return true; }
    double get_value() { return value; }
    void render() {}
};
}
