#include "rtlsdr_sdr.h"
#include "rtlsdr_android.h"
#include <cassert>
#include <chrono>
#include <cstring>
#include <iostream>
#include <thread>

struct rtlsdr_dev { int id; };
static rtlsdr_dev device{1};
static int open_result = 0, rate_result = 0, lookup_result = 0;
static int opens = 0, closes = 0;
static std::atomic<int> reads{0}, cancellations{0}, read_result{99};
static std::atomic<bool> entered{false}, active{false}, cancelled{false}, delay_entry{false}, release_entry{false};
extern "C" {
int rtlsdr_open(rtlsdr_dev_t **out, uint32_t index) { assert(index == 0); ++opens; if (!open_result) *out = &device; return open_result; }
int rtlsdr_close(rtlsdr_dev_t *dev) { assert(dev == &device); ++closes; return 0; }
int rtlsdr_get_tuner_gains(rtlsdr_dev_t *, int *gains) { gains[0] = 0; gains[1] = 496; return 2; }
int rtlsdr_set_sample_rate(rtlsdr_dev_t *, uint32_t) { return rate_result; }
int rtlsdr_set_center_freq(rtlsdr_dev_t *, uint32_t) { return 0; }
int rtlsdr_set_agc_mode(rtlsdr_dev_t *, int) { return 0; }
int rtlsdr_set_tuner_gain_mode(rtlsdr_dev_t *, int) { return 0; }
int rtlsdr_set_tuner_gain(rtlsdr_dev_t *, int) { return 0; }
int rtlsdr_set_bias_tee(rtlsdr_dev_t *, int) { return 0; }
int rtlsdr_set_freq_correction(rtlsdr_dev_t *, int) { return 0; }
int rtlsdr_reset_buffer(rtlsdr_dev_t *) { return 0; }
int rtlsdr_read_async(rtlsdr_dev_t *, rtlsdr_read_async_cb_t, void *, uint32_t, uint32_t) {
    ++reads; entered = true;
    while (delay_entry && !release_entry) std::this_thread::yield();
    active = true;
    while (read_result == 99 && !cancelled) std::this_thread::yield();
    active = false;
    return read_result == 99 ? 0 : read_result.load();
}
int rtlsdr_cancel_async(rtlsdr_dev_t *) { ++cancellations; if (!active) return -2; cancelled = true; return 0; }
int rtlsdr_get_index_by_serial(const char *) { return lookup_result; }
uint32_t rtlsdr_get_device_count() { return 0; }
const char *rtlsdr_get_device_name(uint32_t) { return "Test"; }
int rtlsdr_get_device_usb_strings(uint32_t, char *, char *, char *) { return -3; }
}
#ifdef __ANDROID__
namespace rtl_android {
static int state = 0;
static int requested = 0;
std::vector<Device> devices() { return {{"/dev/bus/usb/001/007", "Generic RTL2832U OEM #0", 0}}; }
int index(const std::string &) { return 0; }
bool permission(const std::string &, bool request) {
    requested += request;
    if (state == -1) throw satdump_exception("USB permission for RTL-SDR was denied");
    if (state == -2) throw satdump_exception("RTL-SDR dongle was disconnected");
    return state == 1;
}
}
#endif
struct Source : RtlSdrSource {
    Source() : RtlSdrSource({"rtlsdr", "test", "/dev/bus/usb/001/007"}) {}
    bool exited() { return thread_exited; }
};
template<class F> void fails(F f, const char *message) {
    try { f(); assert(false); }
    catch (const std::runtime_error &e) { assert(std::strstr(e.what(), message)); }
}
template<class F> void eventually(F f) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!f()) { assert(std::chrono::steady_clock::now() < deadline); std::this_thread::yield(); }
}
void reset_read() { entered = false; active = false; cancelled = false; delay_entry = false; release_entry = false; read_result = 99; }
int main() {
    Source source; source.open();
#ifdef __ANDROID__
    assert(source.needs_async_start());
    assert(RtlSdrSource::getAvailableSources()[0].unique_id == "/dev/bus/usb/001/007");
    assert(opens == 0);
    assert(!source.ready_to_start()); assert(!source.ready_to_start());
    assert(rtl_android::requested == 1 && opens == 0);
    rtl_android::state = -1;
    fails([&] { source.ready_to_start(); }, "was denied");
    rtl_android::state = 0;
    assert(!source.ready_to_start()); assert(rtl_android::requested == 2);
    source.cancel_prepare_start();
    rtl_android::state = 1; assert(source.ready_to_start());
#else
    lookup_result = -3;
    fails([&] { source.start(); }, "was not found");
    assert(opens == 0);
    lookup_result = 0;
#endif
    open_result = -6;
    fails([&] { source.start(); }, "busy (rtlsdr_open code -6)");
    source.stop(); assert(closes == 0);
    open_result = 0; rate_result = -4;
    fails([&] { source.start(); }, "samplerate configuration failed");
    assert(closes == 1);
    rate_result = 0;
    reset_read(); source.start(); eventually([] { return active.load(); }); source.stop();
    assert(closes == 2);
    reset_read(); source.start(); eventually([] { return active.load(); }); source.stop();
    assert(closes == 3);
    // Stop races read_async's transition to RUNNING. The initial cancellation misses it.
    reset_read(); delay_entry = true; source.start(); eventually([] { return entered.load(); });
    int before = cancellations;
    std::thread gate([] { std::this_thread::sleep_for(std::chrono::milliseconds(25)); release_entry = true; });
    source.stop(); gate.join(); assert(cancellations > before + 1 && closes == 4);
    // librtlsdr can return zero on detach. Never re-enter read_async on a dead handle.
    reset_read(); read_result = 0; int previous = reads;
    source.start(); eventually([&] { return source.exited(); });
    assert(reads == previous + 1 && source.output_stream->writer_stopped);
#ifdef __ANDROID__
    assert(source.stream_has_ended());
#endif
    source.stop(); assert(closes == 5);
    reset_read(); read_result = -4;
    source.start(); eventually([&] { return source.exited(); }); source.stop();
    assert(closes == 6);
    source.close();
    std::cout << "RTL source open failures, repeated start/stop, cancellation race, and detach checks passed\n";
}
