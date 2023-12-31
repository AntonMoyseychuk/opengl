#pragma once
#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include "camera.hpp"
#include "renderer.hpp"

#include <string>
#include <memory>

class application {
public:
    application(const std::string_view& title, uint32_t width, uint32_t height);
    ~application();

    void run() noexcept;

private:
    void _destroy() noexcept;

private:
    void _imgui_init(const char* glsl_version) const noexcept;
    void _imgui_shutdown() const noexcept;

    void _imgui_frame_begin() const noexcept;
    void _imgui_frame_end() const noexcept;

private:
    static void _window_resize_callback(GLFWwindow* window, int width, int height) noexcept;
    static void _mouse_callback(GLFWwindow *window, double xpos, double ypos) noexcept;
    static void _mouse_wheel_scroll_callback(GLFWwindow *window, double xoffset, double yoffset) noexcept;

private:
    struct projection_settings {
        float aspect;
        float near;
        float far;
        uint32_t x;
        uint32_t y;
        uint32_t width;
        uint32_t height;

        glm::mat4 projection_mat;
    };

private:
    std::string m_title;
    GLFWwindow* m_window = nullptr;

    projection_settings m_proj_settings;

    camera m_camera;
    renderer m_renderer;

    glm::vec4 m_clear_color = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);

    bool m_wireframed = false;
    bool m_cull_face = false;
};
