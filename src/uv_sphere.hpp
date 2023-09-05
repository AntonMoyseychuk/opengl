#pragma once
#include "mesh.hpp"

class uv_sphere {
public:
    uv_sphere() = default;
    uv_sphere(uint32_t stacks, uint32_t slices);

    void generate(uint32_t stacks, uint32_t slices) noexcept;
    void add_texture(texture&& texture) noexcept;

    const mesh& get_mesh() const noexcept;

private:
    mesh::vertex _create_vertex(float theta, float phi, float tex_s, float tex_t) const noexcept;

public:
    uint32_t stacks;
    uint32_t slices;

private:
    mesh m_mesh;
};