#include "OpenGLViewerWidget.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

#include <QMouseEvent>
#include <QWheelEvent>

namespace {
constexpr float kPi = 3.14159265358979323846f;

Vec3 operator+(const Vec3& lhs, const Vec3& rhs)
{
    return {lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z};
}

Vec3 operator-(const Vec3& lhs, const Vec3& rhs)
{
    return {lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z};
}

Vec3 operator*(const Vec3& value, float scale)
{
    return {value.x * scale, value.y * scale, value.z * scale};
}

float dot(const Vec3& lhs, const Vec3& rhs)
{
    return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
}

Vec3 cross(const Vec3& lhs, const Vec3& rhs)
{
    return {
        lhs.y * rhs.z - lhs.z * rhs.y,
        lhs.z * rhs.x - lhs.x * rhs.z,
        lhs.x * rhs.y - lhs.y * rhs.x,
    };
}

float length(const Vec3& value)
{
    return std::sqrt(dot(value, value));
}

Vec3 normalize(const Vec3& value)
{
    const float valueLength = length(value);
    if (valueLength <= 0.00001f) {
        return {0.0f, 0.0f, 0.0f};
    }
    return value * (1.0f / valueLength);
}

Mat4 identity()
{
    Mat4 matrix;
    matrix.values[0] = 1.0f;
    matrix.values[5] = 1.0f;
    matrix.values[10] = 1.0f;
    matrix.values[15] = 1.0f;
    return matrix;
}

Mat4 multiply(const Mat4& lhs, const Mat4& rhs)
{
    Mat4 result;
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            float value = 0.0f;
            for (int i = 0; i < 4; ++i) {
                value += lhs.values[i * 4 + row] * rhs.values[col * 4 + i];
            }
            result.values[col * 4 + row] = value;
        }
    }
    return result;
}

Mat4 perspective(float fovYRadians, float aspect, float nearPlane, float farPlane)
{
    const float tanHalfFov = std::tan(fovYRadians * 0.5f);
    Mat4 matrix;
    matrix.values[0] = 1.0f / (aspect * tanHalfFov);
    matrix.values[5] = 1.0f / tanHalfFov;
    matrix.values[10] = -(farPlane + nearPlane) / (farPlane - nearPlane);
    matrix.values[11] = -1.0f;
    matrix.values[14] = -(2.0f * farPlane * nearPlane) / (farPlane - nearPlane);
    return matrix;
}

Mat4 lookAt(const Vec3& eye, const Vec3& target, const Vec3& up)
{
    const Vec3 forward = normalize(target - eye);
    const Vec3 right = normalize(cross(forward, up));
    const Vec3 cameraUp = cross(right, forward);

    Mat4 matrix = identity();
    matrix.values[0] = right.x;
    matrix.values[4] = right.y;
    matrix.values[8] = right.z;
    matrix.values[1] = cameraUp.x;
    matrix.values[5] = cameraUp.y;
    matrix.values[9] = cameraUp.z;
    matrix.values[2] = -forward.x;
    matrix.values[6] = -forward.y;
    matrix.values[10] = -forward.z;
    matrix.values[12] = -dot(right, eye);
    matrix.values[13] = -dot(cameraUp, eye);
    matrix.values[14] = dot(forward, eye);
    return matrix;
}
}

OpenGLViewerWidget::OpenGLViewerWidget(QWidget* parent)
    : QOpenGLWidget(parent)
{
    camera_.yawRadians = 0.75f * kPi;
    camera_.pitchRadians = 15.0f * kPi / 180.0f;
    camera_.distance = 4.0f;
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    frameTimer_.setInterval(16);
    connect(&frameTimer_, &QTimer::timeout, this, [this]() {
        updatePlaybackFrame();
        update();
    });
    frameTimer_.start();
}

OpenGLViewerWidget::~OpenGLViewerWidget()
{
    makeCurrent();
    deleteMeshGpu();
    deleteGridGpu();
    if (shaderProgram_ != 0) {
        glDeleteProgram(shaderProgram_);
    }
    doneCurrent();
}

bool OpenGLViewerWidget::loadMotionCache(const std::filesystem::path& cachePath)
{
    MeshCache nextCache;
    if (!loadCache(cachePath, nextCache)) {
        return false;
    }

    cache_ = std::move(nextCache);
    cacheLoaded_ = true;
    isPlaying_ = true;
    currentFrame_ = 0;
    uploadedFrame_ = UINT32_MAX;
    playbackTimer_.restart();
    resetCameraToCache();

    if (glInitialized_) {
        makeCurrent();
        uploadCacheToGpu();
        uploadFrame(0);
        doneCurrent();
    }
    update();
    return true;
}

void OpenGLViewerWidget::initializeGL()
{
    initializeOpenGLFunctions();
    glInitialized_ = true;

    std::cout << "OpenGL version: " << glGetString(GL_VERSION) << '\n';
    std::cout << "Renderer: " << glGetString(GL_RENDERER) << '\n';

    shaderProgram_ = createProgram();
    mvpLocation_ = glGetUniformLocation(shaderProgram_, "uMVP");
    useSolidColorLocation_ = glGetUniformLocation(shaderProgram_, "uUseSolidColor");
    solidColorLocation_ = glGetUniformLocation(shaderProgram_, "uSolidColor");

    glDisable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    uploadGridToGpu();

    if (cacheLoaded_) {
        uploadCacheToGpu();
        uploadFrame(0);
    }
}

void OpenGLViewerWidget::resizeGL(int width, int height)
{
    glViewport(0, 0, std::max(1, width), std::max(1, height));
}

void OpenGLViewerWidget::paintGL()
{
    glClearColor(0.07f, 0.09f, 0.12f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const Mat4 mvp = makeMvp();
    glUseProgram(shaderProgram_);
    glUniformMatrix4fv(mvpLocation_, 1, GL_FALSE, mvp.values.data());

    if (gridGpu_.initialized) {
        glUniform1i(useSolidColorLocation_, 1);
        glUniform3f(solidColorLocation_, 0.25f, 0.29f, 0.34f);
        glDepthMask(GL_FALSE);
        glBindVertexArray(gridGpu_.vao);
        glDrawArrays(GL_LINES, 0, gridGpu_.vertexCount);
        glDepthMask(GL_TRUE);
    }

    if (cacheLoaded_ && meshGpu_.initialized) {
        if (uploadedFrame_ != currentFrame_) {
            uploadFrame(currentFrame_);
        }

        glUniform1i(useSolidColorLocation_, 0);
        glBindVertexArray(meshGpu_.vao);
        glDrawElements(
            GL_TRIANGLES,
            static_cast<GLsizei>(cache_.indexCount),
            GL_UNSIGNED_INT,
            nullptr
        );
    }
}

void OpenGLViewerWidget::mousePressEvent(QMouseEvent* event)
{
    camera_.lastMousePosition = event->pos();
    camera_.hasLastMouse = true;
    event->accept();
}

void OpenGLViewerWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (!camera_.hasLastMouse) {
        camera_.lastMousePosition = event->pos();
        camera_.hasLastMouse = true;
        return;
    }

    const QPoint delta = event->pos() - camera_.lastMousePosition;
    camera_.lastMousePosition = event->pos();

    if (event->buttons() & Qt::LeftButton) {
        constexpr float orbitSensitivity = 0.006f;
        camera_.yawRadians -= static_cast<float>(delta.x()) * orbitSensitivity;
        camera_.pitchRadians += static_cast<float>(delta.y()) * orbitSensitivity;
        const float maxPitch = 85.0f * kPi / 180.0f;
        camera_.pitchRadians = std::clamp(camera_.pitchRadians, -maxPitch, maxPitch);
        update();
    } else if ((event->buttons() & Qt::RightButton) || (event->buttons() & Qt::MiddleButton)) {
        const Vec3 eye = cameraPosition();
        const Vec3 forward = normalize(camera_.target - eye);
        const Vec3 right = normalize(cross(forward, {0.0f, 1.0f, 0.0f}));
        const Vec3 up = cross(right, forward);
        const float panScale = camera_.distance * 0.0015f;
        camera_.target = camera_.target +
            right * static_cast<float>(-delta.x() * panScale) +
            up * static_cast<float>(delta.y() * panScale);
        update();
    }

    event->accept();
}

void OpenGLViewerWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->buttons() == Qt::NoButton) {
        camera_.hasLastMouse = false;
    }
    event->accept();
}

void OpenGLViewerWidget::wheelEvent(QWheelEvent* event)
{
    const float wheelSteps = static_cast<float>(event->angleDelta().y()) / 120.0f;
    if (std::abs(wheelSteps) > 0.0001f) {
        camera_.distance *= std::pow(0.88f, wheelSteps);
        camera_.distance = std::clamp(camera_.distance, camera_.minDistance, camera_.maxDistance);
        update();
    }
    event->accept();
}

GLuint OpenGLViewerWidget::compileShader(GLenum type, const char* source)
{
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint success = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char log[1024] = {};
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        std::cerr << "Shader compile failed: " << log << '\n';
    }

    return shader;
}

GLuint OpenGLViewerWidget::createProgram()
{
    constexpr const char* vertexShaderSource = R"(
#version 330 core
layout (location = 0) in vec3 aPosition;

out vec3 vertexColor;

uniform mat4 uMVP;
uniform bool uUseSolidColor;
uniform vec3 uSolidColor;

void main()
{
    float heightTint = clamp(aPosition.y * 0.45 + 0.55, 0.0, 1.0);
    vec3 heightColor = mix(vec3(0.22, 0.42, 0.72), vec3(0.80, 0.86, 0.92), heightTint);
    vertexColor = uUseSolidColor ? uSolidColor : heightColor;
    gl_Position = uMVP * vec4(aPosition, 1.0);
}
)";

    constexpr const char* fragmentShaderSource = R"(
#version 330 core
in vec3 vertexColor;
out vec4 fragColor;

void main()
{
    fragColor = vec4(vertexColor, 1.0);
}
)";

    const GLuint vertexShader = compileShader(GL_VERTEX_SHADER, vertexShaderSource);
    const GLuint fragmentShader = compileShader(GL_FRAGMENT_SHADER, fragmentShaderSource);

    const GLuint program = glCreateProgram();
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);

    GLint success = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        char log[1024] = {};
        glGetProgramInfoLog(program, sizeof(log), nullptr, log);
        std::cerr << "Program link failed: " << log << '\n';
    }

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
    return program;
}

void OpenGLViewerWidget::uploadGridToGpu()
{
    if (gridGpu_.initialized) {
        return;
    }

    constexpr int halfLineCount = 10;
    constexpr float spacing = 0.5f;
    constexpr float halfSize = static_cast<float>(halfLineCount) * spacing;

    std::vector<float> vertices;
    vertices.reserve(static_cast<std::size_t>((halfLineCount * 2 + 1) * 4 * 3));
    for (int line = -halfLineCount; line <= halfLineCount; ++line) {
        const float offset = static_cast<float>(line) * spacing;

        vertices.push_back(offset);
        vertices.push_back(0.0f);
        vertices.push_back(-halfSize);
        vertices.push_back(offset);
        vertices.push_back(0.0f);
        vertices.push_back(halfSize);

        vertices.push_back(-halfSize);
        vertices.push_back(0.0f);
        vertices.push_back(offset);
        vertices.push_back(halfSize);
        vertices.push_back(0.0f);
        vertices.push_back(offset);
    }

    glGenVertexArrays(1, &gridGpu_.vao);
    glGenBuffers(1, &gridGpu_.vbo);
    gridGpu_.vertexCount = static_cast<GLsizei>(vertices.size() / 3);
    gridGpu_.initialized = true;

    glBindVertexArray(gridGpu_.vao);
    glBindBuffer(GL_ARRAY_BUFFER, gridGpu_.vbo);
    glBufferData(
        GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(vertices.size() * sizeof(float)),
        vertices.data(),
        GL_STATIC_DRAW
    );

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    glEnableVertexAttribArray(0);
}

void OpenGLViewerWidget::uploadCacheToGpu()
{
    if (!meshGpu_.initialized) {
        glGenVertexArrays(1, &meshGpu_.vao);
        glGenBuffers(1, &meshGpu_.vbo);
        glGenBuffers(1, &meshGpu_.ebo);
        meshGpu_.initialized = true;
    }

    glBindVertexArray(meshGpu_.vao);

    glBindBuffer(GL_ARRAY_BUFFER, meshGpu_.vbo);
    glBufferData(
        GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(cache_.vertexCount * 3 * sizeof(float)),
        cache_.vertices.data(),
        GL_DYNAMIC_DRAW
    );

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    glEnableVertexAttribArray(0);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, meshGpu_.ebo);
    glBufferData(
        GL_ELEMENT_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(cache_.indices.size() * sizeof(std::uint32_t)),
        cache_.indices.data(),
        GL_STATIC_DRAW
    );
}

void OpenGLViewerWidget::uploadFrame(std::uint32_t frameIndex)
{
    const std::size_t frameOffset =
        static_cast<std::size_t>(frameIndex) *
        static_cast<std::size_t>(cache_.vertexCount) * 3;

    glBindBuffer(GL_ARRAY_BUFFER, meshGpu_.vbo);
    glBufferSubData(
        GL_ARRAY_BUFFER,
        0,
        static_cast<GLsizeiptr>(cache_.vertexCount * 3 * sizeof(float)),
        cache_.vertices.data() + frameOffset
    );
    uploadedFrame_ = frameIndex;
}

void OpenGLViewerWidget::deleteMeshGpu()
{
    if (meshGpu_.ebo != 0) {
        glDeleteBuffers(1, &meshGpu_.ebo);
    }
    if (meshGpu_.vbo != 0) {
        glDeleteBuffers(1, &meshGpu_.vbo);
    }
    if (meshGpu_.vao != 0) {
        glDeleteVertexArrays(1, &meshGpu_.vao);
    }
    meshGpu_ = {};
}

void OpenGLViewerWidget::deleteGridGpu()
{
    if (gridGpu_.vbo != 0) {
        glDeleteBuffers(1, &gridGpu_.vbo);
    }
    if (gridGpu_.vao != 0) {
        glDeleteVertexArrays(1, &gridGpu_.vao);
    }
    gridGpu_ = {};
}

void OpenGLViewerWidget::resetCameraToCache()
{
    camera_.target = cache_.boundsCenter;
    camera_.yawRadians = 0.5f * kPi;
    camera_.pitchRadians = 10.0f * kPi / 180.0f;
    camera_.distance = std::max(1.5f, cache_.boundsRadius * 2.2f);
    camera_.minDistance = std::max(0.05f, cache_.boundsRadius * 0.15f);
    camera_.maxDistance = std::max(10.0f, cache_.boundsRadius * 12.0f);
    camera_.hasLastMouse = false;
}

Vec3 OpenGLViewerWidget::cameraPosition() const
{
    const float cosPitch = std::cos(camera_.pitchRadians);
    return {
        camera_.target.x + camera_.distance * cosPitch * std::cos(camera_.yawRadians),
        camera_.target.y + camera_.distance * std::sin(camera_.pitchRadians),
        camera_.target.z + camera_.distance * cosPitch * std::sin(camera_.yawRadians),
    };
}

Mat4 OpenGLViewerWidget::makeMvp() const
{
    const int viewportWidth = std::max(1, width());
    const int viewportHeight = std::max(1, height());
    const float aspect = static_cast<float>(viewportWidth) / static_cast<float>(viewportHeight);
    const float nearPlane = std::max(0.01f, camera_.distance * 0.01f);
    const float farPlane = std::max(100.0f, camera_.maxDistance * 4.0f);
    const Mat4 projection = perspective(45.0f * kPi / 180.0f, aspect, nearPlane, farPlane);
    const Mat4 view = lookAt(cameraPosition(), camera_.target, {0.0f, 1.0f, 0.0f});
    return multiply(projection, view);
}

void OpenGLViewerWidget::updatePlaybackFrame()
{
    if (!cacheLoaded_ || !isPlaying_ || cache_.frameCount == 0) {
        return;
    }

    const double elapsedSeconds = static_cast<double>(playbackTimer_.elapsed()) / 1000.0;
    currentFrame_ = static_cast<std::uint32_t>(
        static_cast<std::uint64_t>(elapsedSeconds * cache_.fps) % cache_.frameCount
    );
}
