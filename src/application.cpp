#include "application.hpp"

#include <imgui/imgui.h>
#include <imgui/backends/imgui_impl_glfw.h>
#include <imgui/backends/imgui_impl_opengl3.h>

#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/norm.hpp>

#include "debug.hpp"

#include "shader.hpp"
#include "texture.hpp"
#include "cubemap.hpp"
#include "model.hpp"
#include "framebuffer.hpp"
#include "csm.hpp"

#include "uv_sphere.hpp"

#include "random.hpp"

#include <algorithm>
#include <map>

#include <stb/stb_image.h>

application::application(const std::string_view &title, uint32_t width, uint32_t height)
    : m_title(title), m_camera(glm::vec3(-25.0f, 335.0f, 55.0f), glm::vec3(-25.0f, 335.0f, 55.0f) + glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f), 45.0f, 100.0f, 10.0f)
{
    if (glfwInit() != GLFW_TRUE) {
        const char* glfw_error_msg = nullptr;
        glfwGetError(&glfw_error_msg);

        LOG_ERROR("GLFW error", glfw_error_msg != nullptr ? glfw_error_msg : "unrecognized error");
        exit(-1);
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

// #ifdef _DEBUG
//     glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GL_TRUE);
// #endif

    m_window = glfwCreateWindow(width, height, m_title.c_str(), nullptr, nullptr);
    if (m_window == nullptr) {
        const char* glfw_error_msg = nullptr;
        glfwGetError(&glfw_error_msg);

        LOG_ERROR("GLFW error", glfw_error_msg != nullptr ? glfw_error_msg : "unrecognized error");
        _destroy();
        exit(-1);
    }
    glfwSetWindowUserPointer(m_window, this);

    glfwMakeContextCurrent(m_window);
    ASSERT(gladLoadGLLoader((GLADloadproc)glfwGetProcAddress), "GLAD error", "failed to initialize GLAD");
    glfwSwapInterval(1);

// #ifdef _DEBUG
//     glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
//     glDebugMessageCallback(MessageCallback, nullptr);
// #endif

    glfwSetCursorPosCallback(m_window, &application::_mouse_callback);
    glfwSetScrollCallback(m_window, &application::_mouse_wheel_scroll_callback);

    m_proj_settings.x = m_proj_settings.y = 0;
    m_proj_settings.near = 0.1f;
    m_proj_settings.far = 1500.0f;
    _window_resize_callback(m_window, width, height);
    glfwSetFramebufferSizeCallback(m_window, &_window_resize_callback);

    m_renderer.enable(GL_DEPTH_TEST);
    m_renderer.depth_func(GL_LEQUAL);
    
    // m_renderer.enable(GL_BLEND);
    // m_renderer.blend_func(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    _imgui_init("#version 460 core");
}

std::optional<mesh> generate_terrain(float height_scale, const std::string_view height_map_path) noexcept {
    int32_t width, depth, channel_count;
    uint8_t* height_map = stbi_load(height_map_path.data(), &width, &depth, &channel_count, 0);

    if (width == 0 || depth == 0) {
        stbi_image_free(height_map);
        return std::nullopt;
    }

    const float du = 1.0f / width;
    const float dv = 1.0f / depth;

    std::vector<mesh::vertex> vertices(width * depth);
    for (uint32_t z = 0; z < depth; ++z) {
        for (uint32_t x = 0; x < width; ++x) {
            const size_t index = z * width * channel_count + x * channel_count;
            const float height = (height_map[index]) * height_scale;

            mesh::vertex& vertex = vertices[z * width + x];
            vertex.position = glm::vec3(x, height, z);
            vertex.tangent = glm::vec3(1.0f, 0.0f, 0.0f);
            vertex.texcoord = glm::vec2(x * du, z * dv);
        }
    }

    std::vector<uint32_t> indices;
    indices.reserve((width - 1) * (depth - 1) * 6);
    for (uint32_t z = 0; z < depth - 1; ++z) {
        for (uint32_t x = 0; x < width - 1; ++x) {
            indices.emplace_back(z * width + x);
            indices.emplace_back((z + 1) * width + x);
            indices.emplace_back(z * width + (x + 1));

            indices.emplace_back(z * width + (x + 1));
            indices.emplace_back((z + 1) * width + x);
            indices.emplace_back((z + 1) * width + (x + 1));
        }
    }

    struct vertex_normals_sum {
        glm::vec3 sum = glm::vec3(0.0f);
        uint32_t count = 0;
    };

    std::vector<vertex_normals_sum> averaged_normals(vertices.size());

    for (size_t i = 0; i < indices.size(); i += 3) {
        const glm::vec3 e0 = glm::normalize(vertices[indices[i + 0]].position - vertices[indices[i + 1]].position);
        const glm::vec3 e1 = glm::normalize(vertices[indices[i + 2]].position - vertices[indices[i + 1]].position);
        
        const glm::vec3 normal = glm::cross(e1, e0);

        averaged_normals[indices[i + 0]].sum += normal;
        averaged_normals[indices[i + 1]].sum += normal;
        averaged_normals[indices[i + 2]].sum += normal;
        ++averaged_normals[indices[i + 0]].count;
        ++averaged_normals[indices[i + 1]].count;
        ++averaged_normals[indices[i + 2]].count;
    }

    for (size_t i = 0; i < vertices.size(); ++i) {
        vertices[i].normal = averaged_normals[i].sum / static_cast<float>(averaged_normals[i].count);
    }

    stbi_image_free(height_map);

    return std::move(mesh(vertices, indices));
}

void application::run() noexcept {
    shader skybox_shader(RESOURCE_DIR "shaders/terrain/skybox.vert", RESOURCE_DIR "shaders/terrain/skybox.frag");
    shader terrain_shader(RESOURCE_DIR "shaders/terrain/terrain.vert", RESOURCE_DIR "shaders/terrain/terrain.frag");

    cubemap skybox(std::array<std::string, 6>{
            RESOURCE_DIR "textures/terrain/skybox/right.png",
            RESOURCE_DIR "textures/terrain/skybox/left.png",
            RESOURCE_DIR "textures/terrain/skybox/top.png",
            RESOURCE_DIR "textures/terrain/skybox/bottom.png",
            RESOURCE_DIR "textures/terrain/skybox/front.png",
            RESOURCE_DIR "textures/terrain/skybox/back.png"
        }, false, false
    );
    skybox.set_parameter(GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    skybox.set_parameter(GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    skybox.set_parameter(GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    skybox.set_parameter(GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    model cube(RESOURCE_DIR "models/cube/cube.obj", std::nullopt);

    float skybox_speed = 0.01f;

    float height_scale = 0.7f;
    mesh plane = std::move(generate_terrain(height_scale, RESOURCE_DIR "textures/terrain/height_map.png").value_or(mesh()));

    glm::vec3 light_direction = glm::normalize(glm::vec3(-1.0f, -1.0f, 1.0f));
    glm::vec3 light_color(1.0f);

    ImGuiIO& io = ImGui::GetIO();
    while (!glfwWindowShouldClose(m_window) && glfwGetKey(m_window, GLFW_KEY_ESCAPE) != GLFW_PRESS) {
        glfwPollEvents();

        m_camera.update_dt(io.DeltaTime);
        if (!m_camera.is_fixed) {
            if (glfwGetKey(m_window, GLFW_KEY_SPACE) == GLFW_PRESS) {
                m_camera.move(m_camera.get_up() * io.DeltaTime);
            } else if (glfwGetKey(m_window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS) {
                m_camera.move(-m_camera.get_up() * io.DeltaTime);
            }
            if (glfwGetKey(m_window, GLFW_KEY_W) == GLFW_PRESS) {
                m_camera.move(m_camera.get_forward() * io.DeltaTime);
            } else if (glfwGetKey(m_window, GLFW_KEY_S) == GLFW_PRESS) {
                m_camera.move(-m_camera.get_forward() * io.DeltaTime);
            }
            if (glfwGetKey(m_window, GLFW_KEY_D) == GLFW_PRESS) {
                m_camera.move(m_camera.get_right() * io.DeltaTime);
            } else if (glfwGetKey(m_window, GLFW_KEY_A) == GLFW_PRESS) {
                m_camera.move(-m_camera.get_right() * io.DeltaTime);
            }
        }


        framebuffer::bind_default();
        m_renderer.clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        skybox_shader.uniform("u_projection", m_proj_settings.projection_mat);
        skybox_shader.uniform("u_view", glm::mat4(glm::mat3(m_camera.get_view())));
        const glm::mat4 M = glm::rotate(glm::mat4(1.0f), (float)glfwGetTime() * skybox_speed, glm::vec3(0.0f, 1.0f, 0.0f));
        skybox_shader.uniform("u_model", M);
        skybox_shader.uniform("u_skybox", skybox, 0);
        m_renderer.disable(GL_CULL_FACE);
        m_renderer.render(GL_TRIANGLES, skybox_shader, cube);
        m_cull_face ? m_renderer.enable(GL_CULL_FACE) : m_renderer.disable(GL_CULL_FACE);

        terrain_shader.uniform("u_projection", m_proj_settings.projection_mat);
        terrain_shader.uniform("u_view", m_camera.get_view());
        terrain_shader.uniform("u_model", glm::scale(glm::mat4(1.0f), glm::vec3(2.0f)));
        terrain_shader.uniform("u_camera_position", m_camera.position);
        const glm::vec3 curr_light_direction(M * glm::vec4(light_direction, 0.0f));
        terrain_shader.uniform("u_light.direction", glm::normalize(curr_light_direction));
        terrain_shader.uniform("u_light.color", light_color);
        terrain_shader.uniform("u_material.color", glm::vec3(1.0f));
        terrain_shader.uniform("u_material.shininess", 32.0f);
        m_renderer.render(GL_TRIANGLES, terrain_shader, plane);

    #if 1 //UI
        _imgui_frame_begin();
        ImGui::Begin("Information");
            ImGui::Text("OpenGL version: %s", glGetString(GL_VERSION)); ImGui::NewLine();
                
            if (ImGui::Checkbox("wireframe mode", &m_wireframed)) {
                m_renderer.polygon_mode(GL_FRONT_AND_BACK, (m_wireframed ? GL_LINE : GL_FILL));
            }

            if (ImGui::Checkbox("cull face", &m_cull_face)) {
                m_cull_face ? m_renderer.enable(GL_CULL_FACE) : m_renderer.disable(GL_CULL_FACE);
            }
                
            if (ImGui::NewLine(), ImGui::ColorEdit4("background color", glm::value_ptr(m_clear_color))) {
                m_renderer.set_clear_color(m_clear_color);
            }

            ImGui::Text("average %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);
        ImGui::End();

        ImGui::Begin("Terrain");
            if (ImGui::DragFloat("scale", &height_scale, 0.001f)) {
                height_scale = glm::clamp(height_scale, 0.001f, FLT_MAX);
                plane = std::move(generate_terrain(height_scale, RESOURCE_DIR "textures/terrain/height_map.png").value_or(mesh()));
            }
        ImGui::End();

        ImGui::Begin("Skybox");
            if (ImGui::DragFloat("speed", &skybox_speed, 0.001f)) {
                skybox_speed = glm::clamp(skybox_speed, 0.0f, FLT_MAX);
            }
        ImGui::End();

        // ImGui::Begin("Light");
        //     ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.0f, 0.0f, 1.0f));
        //     if (ImGui::Button("X")) {
        //         light_position.x = 0.0f;
        //     }
        //     ImGui::PushItemWidth(70);
        //     ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.6f, 0.0f, 1.0f));
        //     ImGui::SameLine(); ImGui::DragFloat("##X", &light_position.x, 0.1f);
        //     if ((ImGui::SameLine(), ImGui::Button("Y"))) {
        //         light_position.y = 0.0f;
        //     }
        //     ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.6f, 1.0f));
        //     ImGui::SameLine(); ImGui::DragFloat("##Y", &light_position.y, 0.1f);
        //     if ((ImGui::SameLine(), ImGui::Button("Z"))) {
        //         light_position.z = 0.0f;
        //     }
        //     ImGui::SameLine(); ImGui::DragFloat("##Z", &light_position.z, 0.1f);
        //     ImGui::PopStyleColor(3);
        //     ImGui::PopItemWidth();
        //     ImGui::ColorEdit3("color", glm::value_ptr(light_color));
        //     ImGui::DragFloat("intensity", &intensity, 0.1f);
        // ImGui::End();

        // ImGui::Begin("Backpack");
        //     ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.0f, 0.0f, 1.0f));
        //     if (ImGui::Button("X")) {
        //         backpack_position.x = 0.0f;
        //     }
        //     ImGui::PushItemWidth(70);
        //     ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.6f, 0.0f, 1.0f));
        //     ImGui::SameLine(); ImGui::DragFloat("##X", &backpack_position.x, 0.1f);
        //     if ((ImGui::SameLine(), ImGui::Button("Y"))) {
        //         backpack_position.y = 0.0f;
        //     }
        //     ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.6f, 1.0f));
        //     ImGui::SameLine(); ImGui::DragFloat("##Y", &backpack_position.y, 0.1f);
        //     if ((ImGui::SameLine(), ImGui::Button("Z"))) {
        //         backpack_position.z = 0.0f;
        //     }
        //     ImGui::SameLine(); ImGui::DragFloat("##Z", &backpack_position.z, 0.1f);
        //     ImGui::PopStyleColor(3);
        //     ImGui::PopItemWidth();
        // ImGui::End();

        // ImGui::Begin("Texture");
        //     const uint32_t buffers[] = { position_buffer.get_id(), color_buffer.get_id(), normal_buffer.get_id(), ssao_buffer.get_id(), ssaoblur_buffer.get_id() };
            
        //     ImGui::SliderInt("buffer number", &buffer_number, 0, sizeof(buffers) / sizeof(buffers[0]) - 1);
        //     ImGui::Image(
        //         (void*)(intptr_t)buffers[buffer_number], 
        //         ImVec2(position_buffer.get_width(), position_buffer.get_height()), 
        //         ImVec2(0, 1), 
        //         ImVec2(1, 0)
        //     );
        // ImGui::End();

        ImGui::Begin("Camera");
            ImGui::DragFloat3("position", glm::value_ptr(m_camera.position), 0.1f);
            if (ImGui::DragFloat("speed", &m_camera.speed, 0.1f) || 
                ImGui::DragFloat("sensitivity", &m_camera.sensitivity, 0.1f)
            ) {
                m_camera.speed = std::clamp(m_camera.speed, 1.0f, std::numeric_limits<float>::max());
                m_camera.sensitivity = std::clamp(m_camera.sensitivity, 0.1f, std::numeric_limits<float>::max());
            }
            if (ImGui::SliderFloat("field of view", &m_camera.fov, 1.0f, 179.0f)) {
                _window_resize_callback(m_window, m_proj_settings.width, m_proj_settings.height);
            }

            if (ImGui::Checkbox("fixed (Press \'F\')", &m_camera.is_fixed)) {
                glfwSetInputMode(m_window, GLFW_CURSOR, (m_camera.is_fixed ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED));
            } else if (glfwGetKey(m_window, GLFW_KEY_F) == GLFW_PRESS) {
                m_camera.is_fixed = !m_camera.is_fixed;

                glfwSetInputMode(m_window, GLFW_CURSOR, (m_camera.is_fixed ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED));
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        ImGui::End();
        _imgui_frame_end();
    #endif

        glfwSwapBuffers(m_window);
    }
}

application::~application() {
    _imgui_shutdown();
    _destroy();
}

void application::_destroy() noexcept {
    // NOTE: causes invalid end of program since it't called before all OpenGL objects are deleted
    // glfwDestroyWindow(m_window);
    // glfwTerminate();
}

void application::_imgui_init(const char *glsl_version) const noexcept {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(m_window, true);
    ImGui_ImplOpenGL3_Init("#version 460 core");
}

void application::_imgui_shutdown() const noexcept {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

void application::_imgui_frame_begin() const noexcept {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void application::_imgui_frame_end() const noexcept {
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void application::_window_resize_callback(GLFWwindow *window, int width, int height) noexcept {
    application* app = static_cast<application*>(glfwGetWindowUserPointer(window));
    ASSERT(app != nullptr, "", "application instance is not set");

    if (width != 0 && height != 0) {
        app->m_renderer.viewport(app->m_proj_settings.x, app->m_proj_settings.y, width, height);

        app->m_proj_settings.width = width;
        app->m_proj_settings.height = height;
        
        app->m_proj_settings.aspect = static_cast<float>(width) / height;
        
        const camera& active_camera = app->m_camera;
        
        app->m_proj_settings.projection_mat = glm::perspective(glm::radians(active_camera.fov), app->m_proj_settings.aspect, app->m_proj_settings.near, app->m_proj_settings.far);
    }
}

void application::_mouse_callback(GLFWwindow *window, double xpos, double ypos) noexcept {
    application* app = static_cast<application*>(glfwGetWindowUserPointer(window));
    ASSERT(app != nullptr, "", "application instance is not set");

    app->m_camera.mouse_callback(xpos, ypos);
}

void application::_mouse_wheel_scroll_callback(GLFWwindow *window, double xoffset, double yoffset) noexcept {
    application* app = static_cast<application*>(glfwGetWindowUserPointer(window));
    ASSERT(app != nullptr, "", "application instance is not set");

    app->m_camera.wheel_scroll_callback(yoffset);
    
    _window_resize_callback(window, app->m_proj_settings.width, app->m_proj_settings.height);
}
