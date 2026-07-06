#include "utils/GpuElapsedTimer.h"

#include <algorithm>
#include <iostream>
#include <utility>

void GpuElapsedTimer::initialize(std::string label,
                                 std::uint32_t log_interval,
                                 QOpenGLFunctions_4_5_Core& gl)
{
    release(gl);

    label_ = std::move(label);
    log_interval_ = std::max(log_interval, 1u);
    gl.glGenQueries(static_cast<GLsizei>(queries_.size()), queries_.data());
    query_pending_.fill(false);
    next_query_ = 0u;
    active_ = false;
    sample_count_ = 0u;
    window_sample_count_ = 0u;
    window_total_ms_ = 0.0;

    if (is_initialized()) {
        std::cerr << "[GPU TIMING] " << label_ << " GL_TIME_ELAPSED enabled.\n";
    }
}

void GpuElapsedTimer::release(QOpenGLFunctions_4_5_Core& gl)
{
    if (active_) {
        gl.glEndQuery(GL_TIME_ELAPSED);
        active_ = false;
    }

    if (queries_[0] != 0u) {
        gl.glDeleteQueries(static_cast<GLsizei>(queries_.size()), queries_.data());
    }

    label_.clear();
    queries_.fill(0u);
    query_pending_.fill(false);
    next_query_ = 0u;
    sample_count_ = 0u;
    window_sample_count_ = 0u;
    window_total_ms_ = 0.0;
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

    gl.glBeginQuery(GL_TIME_ELAPSED, queries_[query_index]);
    query_pending_[query_index] = true;
    active_ = true;
    next_query_ = (next_query_ + 1u) % queries_.size();
    return true;
}

void GpuElapsedTimer::end(QOpenGLFunctions_4_5_Core& gl) const
{
    if (!active_) {
        return;
    }

    gl.glEndQuery(GL_TIME_ELAPSED);
    active_ = false;
}

void GpuElapsedTimer::collect(QOpenGLFunctions_4_5_Core& gl) const
{
    if (!is_initialized()) {
        return;
    }

    for (std::size_t query_index = 0u; query_index < queries_.size(); ++query_index) {
        if (!query_pending_[query_index]) {
            continue;
        }

        GLuint is_available = GL_FALSE;
        gl.glGetQueryObjectuiv(queries_[query_index], GL_QUERY_RESULT_AVAILABLE, &is_available);
        if (is_available == GL_FALSE) {
            continue;
        }

        GLuint64 elapsed_nanoseconds = 0u;
        gl.glGetQueryObjectui64v(queries_[query_index], GL_QUERY_RESULT, &elapsed_nanoseconds);
        query_pending_[query_index] = false;

        const double elapsed_ms =
            static_cast<double>(elapsed_nanoseconds) * nanoseconds_to_milliseconds;
        ++sample_count_;
        ++window_sample_count_;
        window_total_ms_ += elapsed_ms;

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
    return queries_[0] != 0u;
}
