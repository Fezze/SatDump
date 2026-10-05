#pragma once
// Mock only UI/DSP plumbing. The test compiles the production RtlSdrSource implementation.
#include "core/exception.h"
#include "nlohmann/json_utils.h"
#include <atomic>
#include <algorithm>
#include <memory>
#include <string>
#include <vector>
struct complex_t { float r, i; float &real = r, &imag = i; };
namespace dsp {
template<class T> struct stream {
    T writeBuf[16];
    std::atomic<bool> writer_stopped{false}, reader_stopped{false};
    void swap(int) {}
    void stopWriter() { writer_stopped = true; }
    void stopReader() { reader_stopped = true; }
};
struct SourceDescriptor { std::string source_type, name, unique_id; };
struct DSPSampleSource {
    std::string d_sdr_id;
    uint64_t d_frequency = 100000000;
    nlohmann::json d_settings;
    std::shared_ptr<stream<complex_t>> output_stream;
    DSPSampleSource(SourceDescriptor d) : d_sdr_id(d.unique_id) {}
    virtual bool needs_async_start() { return false; }
    virtual bool ready_to_start() { return true; }
    virtual void cancel_prepare_start() {}
    virtual bool stream_has_ended() { return false; }
    virtual void start() { output_stream = std::make_shared<stream<complex_t>>(); }
    virtual void set_frequency(uint64_t f) { d_frequency = f; }
    int calculate_buffer_size_from_samplerate(int) { return 8192; }
};
}
