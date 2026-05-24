#pragma once

#include <filesystem>
#include <utility>

#include <QOpenGLWidget>
#include <QString>

inline QString to_q_string(const std::filesystem::path& path)
{
    return QString::fromStdWString(path.wstring());
}

class ScopedGlContext {
public:
    explicit ScopedGlContext(QOpenGLWidget& widget)
        : widget_(widget)
    {
        widget_.makeCurrent();
    }

    ScopedGlContext(const ScopedGlContext&) = delete;
    ScopedGlContext& operator=(const ScopedGlContext&) = delete;

    ~ScopedGlContext()
    {
        widget_.doneCurrent();
    }

private:
    QOpenGLWidget& widget_;
};

template <typename Func>
decltype(auto) run_with_gl_context(QOpenGLWidget& widget, Func&& func)
{
    ScopedGlContext context(widget);
    return std::forward<Func>(func)();
}
