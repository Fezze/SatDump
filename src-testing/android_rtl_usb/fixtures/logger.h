#pragma once
struct TestLogger {
    template<class... T> void trace(T...) {}
    template<class... T> void debug(T...) {}
    template<class... T> void info(T...) {}
    template<class... T> void warn(T...) {}
    template<class... T> void error(T...) {}
};
inline TestLogger test_logger;
inline TestLogger *logger = &test_logger;
