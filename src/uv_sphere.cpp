#include "uv_sphere.hpp"
#include <glm/gtc/constants.hpp>

uv_sphere::uv_sphere(uint32_t stacks, uint32_t slices) {
    this->generate(stacks, slices);
}

void uv_sphere::generate(uint32_t stacks, uint32_t slices) noexcept {
    this->stacks = stacks;
    this->slices = slices;

    const float theta_step = glm::pi<float>() / this->stacks;
    const float phi_step = glm::two_pi<float>() / this->slices;

    std::vector<mesh::vertex> vertices;
    vertices.reserve((this->stacks - 1) * this->slices + 2);

    vertices.emplace_back(mesh::vertex { glm::vec3(0.0f, 1.0f, 0.0f), {}, {} });
    for (uint32_t stack = 0; stack < this->stacks - 1; ++stack) {
        const float theta = (stack + 1) * theta_step;

        for (uint32_t slice = 0; slice < this->slices; ++slice) {
            const float phi = slice * phi_step;

            mesh::vertex v;
            v.position.x = glm::sin(theta) * glm::sin(phi);
            v.position.y = glm::cos(theta);
            v.position.z = glm::sin(theta) * glm::cos(phi);

            vertices.emplace_back(v);
        }
    }
    vertices.emplace_back(mesh::vertex { glm::vec3(0.0f, -1.0f, 0.0f), {}, {} });

    std::vector<uint32_t> indices;
    indices.reserve((2 * slices * 3) + ((stacks - 2) * slices * 6));

    for (uint32_t i = 0; i < slices; ++i) {
        const uint32_t a = i + 1;
        const uint32_t b = (i + 1) % slices + 1;
        
        indices.emplace_back(0);
        indices.emplace_back(b);
        indices.emplace_back(a);
    }

    for(uint32_t i = 0; i < stacks - 2; ++i) {	
		uint32_t a_start = i * slices;
		uint32_t b_start = (i + 1) * slices;
		
        for(uint32_t j = 0; j < slices; ++j) {
			uint32_t a0 = a_start + j + 1;
			uint32_t a1 = a_start + (j + 1) % slices + 1;
			uint32_t b0 = b_start + j + 1;
			uint32_t b1 = b_start + (j + 1) % slices + 1;

            indices.emplace_back(a0);
            indices.emplace_back(a1);
            indices.emplace_back(b1);

            indices.emplace_back(a0);
            indices.emplace_back(b1);
            indices.emplace_back(b0);
		}
	}

    const uint32_t bottom_index = vertices.size() - 1;
    for (uint32_t i = 0; i < slices; ++i) {
        const uint32_t a = i + bottom_index - slices;
        const uint32_t b = (i + 1) % slices + bottom_index - slices;
        
        indices.emplace_back(bottom_index);
        indices.emplace_back(a);
        indices.emplace_back(b);
    }

    ASSERT(indices.size() >= 3, "uv sphere generation error", "index count < 3");
    ASSERT(indices.size() % 3 == 0, "uv sphere generation error", "index count % 3 != 0");

    for (size_t i = 2; i < indices.size(); i += 3) {
        const glm::vec3 a = vertices[indices[i - 0]].position - vertices[indices[i - 2]].position;
        const glm::vec3 b = vertices[indices[i - 1]].position - vertices[indices[i - 2]].position;
        const glm::vec3 normal = glm::normalize(glm::cross(a, b));

        vertices[indices[i - 0]].normal = vertices[indices[i - 1]].normal = vertices[indices[i - 2]].normal = normal;
    }

    m_mesh.create(vertices, indices, {});
}

void uv_sphere::draw(const shader &shader) const noexcept {
    m_mesh.draw(shader);
}

const mesh &uv_sphere::get_mesh() const noexcept {
    return m_mesh;
}