#pragma once

#include "MotionCache.h"

#include <array>
#include <cstdint>
#include <filesystem>

#include <QElapsedTimer>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLWidget>
#include <QPoint>
#include <QTimer>

struct Mat4 {
    std::array<float, 16> values = {};
};

struct MeshGpu {
    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint ebo = 0;
    bool initialized = false;
};

struct GridGpu {
    GLuint vao = 0;
    GLuint vbo = 0;
    GLsizei vertexCount = 0;
    bool initialized = false;
};

struct OrbitCamera {
    Vec3 target;
    float yawRadians = 0.0f;
    float pitchRadians = 0.0f;
    float distance = 3.0f;
    float minDistance = 0.25f;
    float maxDistance = 50.0f;
    QPoint lastMousePosition;
    bool hasLastMouse = false;
};

class OpenGLViewerWidget final : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core {
public:
    explicit OpenGLViewerWidget(QWidget* parent = nullptr);
    ~OpenGLViewerWidget() override;

    bool loadMotionCache(const std::filesystem::path& cachePath);

protected:
    void initializeGL() override;
    void resizeGL(int width, int height) override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    GLuint compileShader(GLenum type, const char* source);
    GLuint createProgram();
    void uploadGridToGpu();
    void uploadCacheToGpu();
    void uploadFrame(std::uint32_t frameIndex);
    void deleteMeshGpu();
    void deleteGridGpu();
    void resetCameraToCache();
    Vec3 cameraPosition() const;
    Mat4 makeMvp() const;
    void updatePlaybackFrame();

    MeshCache cache_;
    MeshGpu meshGpu_;
    GridGpu gridGpu_;
    OrbitCamera camera_;
    GLuint shaderProgram_ = 0;
    GLint mvpLocation_ = -1;
    GLint useSolidColorLocation_ = -1;
    GLint solidColorLocation_ = -1;
    bool glInitialized_ = false;
    bool cacheLoaded_ = false;
    bool isPlaying_ = false;
    std::uint32_t currentFrame_ = 0;
    std::uint32_t uploadedFrame_ = UINT32_MAX;
    QElapsedTimer playbackTimer_;
    QTimer frameTimer_;
};
