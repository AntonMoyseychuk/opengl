#include "cubemap.hpp"

#include "debug.hpp"

#include <glad/glad.h>
#include <stb/stb_image.h>

#include <algorithm>

cubemap::cubemap(uint32_t width, uint32_t height, uint32_t level, uint32_t internal_format, uint32_t format, uint32_t type, 
    const std::array<uint8_t*, 6> &pixels
) {
    create(width, height, level, internal_format, format, type, pixels);
}

cubemap::cubemap(const std::array<std::string, 6>& faces, bool flip_on_load, bool use_gamma) {
    load(faces, flip_on_load, use_gamma);
}

cubemap::~cubemap() {
    destroy();
}

void cubemap::load(const std::array<std::string, 6>& faces, bool flip_on_load, bool use_gamma) noexcept {
    stbi_set_flip_vertically_on_load(flip_on_load);

    std::array<uint8_t*, 6> pixels;
    std::array<int32_t, 6> face_widths;
    std::array<int32_t, 6> face_heights;

    int32_t channel_count = 0;
    for (size_t i = 0; i < pixels.size(); ++i) {
        pixels[i] = stbi_load(faces[i].c_str(), &face_widths[i], &face_heights[i], &channel_count, 0);
        
        ASSERT(pixels[i] != nullptr, "cubemap creation error", "couldn't load texture \"" + faces[i] + "\"");
    }

#ifdef _DEBUG
    if (std::count_if(face_widths.cbegin(), face_widths.cend(), [&face_widths](int32_t width) { return width != face_widths[0]; })) {
        LOG_WARN("cubemap", "faces in cubemap have different sizes");
    }
    if (std::count_if(face_heights.cbegin(), face_heights.cend(), [&face_heights](int32_t height) { return height != face_heights[0]; })) {
        LOG_WARN("cubemap", "faces in cubemap have different sizes");
    }
#endif

    const uint32_t internal_format = _get_gl_format(channel_count, use_gamma);
    const uint32_t format = _get_gl_format(channel_count, false);

    create(face_widths[0], face_heights[0], 0, internal_format, format, GL_UNSIGNED_BYTE, pixels);
}

void cubemap::create(uint32_t width, uint32_t height, uint32_t level, uint32_t internal_format, uint32_t format, uint32_t type,
                     const std::array<uint8_t *, 6> &pixels) noexcept
{
    if (m_data.id != 0) {
        LOG_WARN("cubemap warning", "cubemap recreation (prev id = " + std::to_string(m_data.id) + ")");
        destroy();
    }

    m_data.width = width;
    m_data.height = height;

    OGL_CALL(glGenTextures(1, &m_data.id));
    bind();

    for (size_t i = 0; i < pixels.size(); ++i) {
        OGL_CALL(glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, level, internal_format, m_data.width, m_data.height, 0, format, type, pixels[i]));
    }
}

void cubemap::destroy() noexcept {
    OGL_CALL(glDeleteTextures(1, &m_data.id));
    m_data.id = 0;
}

void cubemap::bind(int32_t unit) const noexcept {
#ifdef _DEBUG
    int32_t max_units_count;
    OGL_CALL(glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &max_units_count));
    ASSERT(unit < max_units_count, "texture error", "unit value is greater than GL_MAX_TEXTURE_UNITS");
#endif
    if (unit >= 0) {
        const_cast<cubemap*>(this)->m_data.texture_unit = unit;
        OGL_CALL(glActiveTexture(GL_TEXTURE0 + unit));
    }

    OGL_CALL(glBindTexture(GL_TEXTURE_CUBE_MAP, m_data.id));
}

void cubemap::unbind() const noexcept {
    OGL_CALL(glBindTexture(GL_TEXTURE_CUBE_MAP, 0));
}

void cubemap::generate_mipmap() const noexcept {
    OGL_CALL(glGenerateMipmap(GL_TEXTURE_CUBE_MAP));
}

void cubemap::set_parameter(uint32_t pname, int32_t param) const noexcept {
    OGL_CALL(glTexParameteri(GL_TEXTURE_CUBE_MAP, pname, param));
}

void cubemap::set_parameter(uint32_t pname, float param) const noexcept {
    OGL_CALL(glTexParameterf(GL_TEXTURE_CUBE_MAP, pname, param));
}

void cubemap::set_parameter(uint32_t pname, const float *params) const noexcept {
    OGL_CALL(glTexParameterfv(GL_TEXTURE_CUBE_MAP, pname, params));
}

uint32_t cubemap::get_id() const noexcept {
    return m_data.id;
}

uint32_t cubemap::get_unit() const noexcept {
    return m_data.texture_unit;
}

uint32_t cubemap::get_width() const noexcept {
    return m_data.width;
}

uint32_t cubemap::get_height() const noexcept {
    return m_data.height;
}

cubemap::cubemap(cubemap &&cubemap)
    : m_data(cubemap.m_data)
{
    if (this != &cubemap) {
        memset(&cubemap.m_data, 0, sizeof(cubemap.m_data));
    }
}

cubemap &cubemap::operator=(cubemap &&cubemap) noexcept {
    if (this != &cubemap) {
        m_data = cubemap.m_data;
        memset(&cubemap.m_data, 0, sizeof(cubemap.m_data));
    }
    
    return *this;
}

int32_t cubemap::_get_gl_format(int32_t channel_count, bool use_gamma) const noexcept {
    switch (channel_count) {
    case 1:
        return GL_RED;

    case 3:
        return use_gamma ? GL_SRGB : GL_RGB;

    case 4:
        return use_gamma ? GL_SRGB_ALPHA : GL_RGBA;
    
    default:
        ASSERT(false, "texture_2d", "invalid channel count");
        return 0;
    }
}
