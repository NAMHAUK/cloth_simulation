#include "utils/GpuElapsedTimer.h"

#include <algorithm>
#include <iostream>
#include <utility>

void GpuElapsedTimer::initialize(std::string label,
                                 std::uint32_t log_interval,
                                 QOpenGLFunctions_4_5_Core& gl)
{
    initialize(std::move(label), log_interval, 1u, gl);
}

void GpuElapsedTimer::initialize(std::string label,
                                 std::uint32_t log_interval,
                                 std::uint32_t calls_per_sample,
                                 QOpenGLFunctions_4_5_Core& gl)
{
    release(gl);

    label_ = std::move(label);
    log_interval_ = std::max(log_interval, 1u);
    calls_per_sample_ = std::max(calls_per_sample, 1u);
    gl.glGenQueries(static_cast<GLsizei>(start_queries_.size()), start_queries_.data());
    gl.glGenQueries(static_cast<GLsizei>(end_queries_.size()), end_queries_.data());
    query_pending_.fill(false);
    next_query_ = 0u;
    active_query_ = 0u;
    active_ = false;
    sample_count_ = 0u;
    window_sample_count_ = 0u;
    window_total_ms_ = 0.0;
    group_call_count_ = 0u;
    group_total_ms_ = 0.0;

    if (is_initialized()) {
        std::cerr << "[GPU TIMING] " << label_ << " timestamp timing enabled.\n";
    }
}

void GpuElapsedTimer::release(QOpenGLFunctions_4_5_Core& gl)
{
    if (start_queries_[0] != 0u) {
        gl.glDeleteQueries(static_cast<GLsizei>(start_queries_.size()), start_queries_.data());
    }
    if (end_queries_[0] != 0u) {
        gl.glDeleteQueries(static_cast<GLsizei>(end_queries_.size()), end_queries_.data());
    }

    label_.clear();
    calls_per_sample_ = 1u;
    start_queries_.fill(0u);
    end_queries_.fill(0u);
    query_pending_.fill(false);
    next_query_ = 0u;
    active_query_ = 0u;
    active_ = false;
    sample_count_ = 0u;
    window_sample_count_ = 0u;
    window_total_ms_ = 0.0;
    group_call_count_ = 0u;
    group_total_ms_ = 0.0;
}

bool GpuElapsedTimer::begin(QOpenGLFunctions_4_5_Core& gl) const
{
    if (!is_initialized() || active_) {
        return false;
    }

    collect(gl);

    const std::size_t query_index = next_query_;
    if (query_pending_[query_index]) {
        return false;
    }

    gl.glQueryCounter(start_queries_[query_index], GL_TIMESTAMP);
    query_pending_[query_index] = true;
    active_query_ = query_index;
    active_ = true;
    next_query_ = (next_query_ + 1u) % start_queries_.size();
    return true;
}

void GpuElapsedTimer::end(QOpenGLFunctions_4_5_Core& gl) const
{
    if (!active_) {
        return;
    }

    gl.glQueryCounter(end_queries_[active_query_], GL_TIMESTAMP);
    active_ = false;
}

void GpuElapsedTimer::collect(QOpenGLFunctions_4_5_Core& gl) const
{
    if (!is_initialized()) {
        return;
    }

    for (std::size_t query_index = 0u; query_index < end_queries_.size(); ++query_index) {
        if (!query_pending_[query_index]) {
            continue;
        }

        GLuint is_available = GL_FALSE;
        gl.glGetQueryObjectuiv(end_queries_[query_index], GL_QUERY_RESULT_AVAILABLE, &is_available);
        if (is_available == GL_FALSE) {
            continue;
        }

        GLuint64 start_nanoseconds = 0u;
        GLuint64 end_nanoseconds = 0u;
        gl.glGetQueryObjectui64v(start_queries_[query_index], GL_QUERY_RESULT, &start_nanoseconds);
        gl.glGetQueryObjectui64v(end_queries_[query_index], GL_QUERY_RESULT, &end_nanoseconds);
        query_pending_[query_index] = false;

        const GLuint64 elapsed_nanoseconds =
            end_nanoseconds > start_nanoseconds ? end_nanoseconds - start_nanoseconds : 0u;
        const double elapsed_ms =
            static_cast<double>(elapsed_nanoseconds) * nanoseconds_to_milliseconds;
        ++group_call_count_;
        group_total_ms_ += elapsed_ms;
        if (group_call_count_ < calls_per_sample_) {
            continue;
        }

        ++sample_count_;
        ++window_sample_count_;
        window_total_ms_ += group_total_ms_;
        group_call_count_ = 0u;
        group_total_ms_ = 0.0;

        if (window_sample_count_ >= log_interval_) {
            const double average_ms = window_total_ms_ / static_cast<double>(window_sample_count_);
            std::cerr << "[GPU TIMING] " << label_ << " average " << average_ms
                      << " ms over " << window_sample_count_
                      << " samples, total samples " << sample_count_ << ".\n";
            window_sample_count_ = 0u;
            window_total_ms_ = 0.0;
        }
    }
}

bool GpuElapsedTimer::is_initialized() const
{
    return start_queries_[0] != 0u && end_queries_[0] != 0u;
}
