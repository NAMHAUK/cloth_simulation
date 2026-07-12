#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include <QOpenGLFunctions_4_5_Core>

class GpuElapsedTimer final {
public:
    void initialize(std::string label, std::uint32_t log_interval, QOpenGLFunctions_4_5_Core& gl);
    void initialize(std::string label,
                    std::uint32_t log_interval,
                    std::uint32_t calls_per_sample,
                    QOpenGLFunctions_4_5_Core& gl);
    void release(QOpenGLFunctions_4_5_Core& gl);

    bool begin(QOpenGLFunctions_4_5_Core& gl) const;
    void end(QOpenGLFunctions_4_5_Core& gl) const;
    void collect(QOpenGLFunctions_4_5_Core& gl) const;

    bool is_initialized() const;

private:
    static constexpr std::size_t query_count = 8u;
    static constexpr double nanoseconds_to_milliseconds = 1.0e-6;

    std::string label_;
    std::uint32_t log_interval_ = 1u;
    std::uint32_t calls_per_sample_ = 1u;
    mutable std::array<GLuint, query_count> start_queries_{};
    mutable std::array<GLuint, query_count> end_queries_{};
    mutable std::array<bool, query_count> query_pending_{};
    mutable std::size_t next_query_ = 0u;
    mutable std::size_t active_query_ = 0u;
    mutable bool active_ = false;
    mutable std::uint64_t sample_count_ = 0u;
    mutable std::uint32_t window_sample_count_ = 0u;
    mutable double window_total_ms_ = 0.0;
    mutable std::uint32_t group_call_count_ = 0u;
    mutable double group_total_ms_ = 0.0;
};
