#version 430 core

out vec4 frag_color;

in VS_OUT {
    vec3 frag_pos;
    vec3 normal;
    vec2 texcoord;
} fs_in;

struct PointLight {
    vec3 position;

    vec3 ambient;
    vec3 diffuse;
    vec3 specular;

    float linear;
    float quadratic;
};

struct DirectionalLight {
    vec3 direction;

    vec3 ambient;
    vec3 diffuse;
    vec3 specular;
};

struct SpotLight {
    vec3 position;
    vec3 direction;

    vec3 ambient;
    vec3 diffuse;
    vec3 specular;

    float cutoff;
};

struct Material {
    sampler2D diffuse0;
    sampler2D specular0;
    sampler2D emission0;

    float shininess;
};

#define MAX_POINT_LIGTHS_COUNT 10
#define MAX_SPOT_LIGTHS_COUNT 10

uniform uint u_point_lights_count;
uniform uint u_spot_lights_count;

uniform DirectionalLight u_dir_light;
uniform PointLight u_point_lights[MAX_POINT_LIGTHS_COUNT];
uniform SpotLight u_spot_lights[MAX_SPOT_LIGTHS_COUNT];

uniform Material u_material;

uniform vec3 u_view_position;
uniform bool u_use_blinn_phong;

vec3 calculate_ambient(vec3 light_ambient, vec3 ambient_map) {
    return light_ambient * ambient_map;
}

vec3 calculate_diffuse(vec3 light_direction, vec3 normal, vec3 light_diffuse, vec3 diffuse_map) {
    const float diff = max(dot(normal, -light_direction), 0.0);
    return light_diffuse * diff * diffuse_map;
}

vec3 calculate_specular(bool use_blinn_phong_model, vec3 view_direction, vec3 light_direction, vec3 normal, vec3 light_specular, vec3 specular_map) {
    if (use_blinn_phong_model) {
        const vec3 half_direction = normalize(light_direction + view_direction);
        const float spec = pow(max(dot(normal, -half_direction), 0.0), u_material.shininess);
        return light_specular * spec * specular_map;
    } else {
        const vec3 reflect_direction = normalize(reflect(light_direction, normal));
        const float spec = pow(max(dot(-view_direction, reflect_direction), 0.0), u_material.shininess);
        return light_specular * spec * specular_map;
    }
}

vec3 calculate_directional_light(DirectionalLight light, vec3 normal, vec3 diffuse_map, vec3 specular_map) {
    vec3 color = calculate_ambient(light.ambient, diffuse_map);   

    const vec3 light_direction = normalize(light.direction);
    color += calculate_diffuse(light_direction, normal, light.diffuse, diffuse_map);

    const vec3 view_direction = normalize(fs_in.frag_pos - u_view_position);
    color += calculate_specular(u_use_blinn_phong, view_direction, light_direction, normal, 
        light.specular, specular_map);

    return color;
}

vec3 calculate_point_light(PointLight light, vec3 normal, vec3 diffuse_map, vec3 specular_map) {
    const float dist = length(light.position - fs_in.frag_pos);
    const float attenuation = 1.0 / (1.0 + light.linear * dist + light.quadratic * (dist * dist));

    vec3 color = calculate_ambient(light.ambient, diffuse_map) * attenuation;

    const vec3 light_direction = normalize(fs_in.frag_pos - light.position);
    color += calculate_diffuse(light_direction, normal, light.diffuse, diffuse_map) * attenuation;

    const vec3 view_direction = normalize(fs_in.frag_pos - u_view_position);
    color += calculate_specular(u_use_blinn_phong, view_direction, light_direction, normal, 
        light.specular, specular_map) * attenuation;

    return color;
}

vec3 calculate_spot_light(SpotLight light, vec3 normal, vec3 diffuse_map, vec3 specular_map) {
    vec3 color = calculate_ambient(light.ambient, diffuse_map);
    
    const vec3 light_direction = normalize(fs_in.frag_pos - light.position);

    const float theta = dot(light_direction, normalize(light.direction));
    if (theta > light.cutoff) {
        color += calculate_diffuse(light_direction, normal, light.diffuse, diffuse_map);

        const vec3 view_direction = normalize(fs_in.frag_pos - u_view_position);
        color += calculate_specular(u_use_blinn_phong, view_direction, light_direction, normal, 
            light.specular, specular_map);
    }
    
    return color;
}

uniform bool u_is_sphere;

void main() {
    const vec4 diffuse_map = u_is_sphere ? vec4(1.0f, 0.0f, 0.0f, 1.0f) : texture(u_material.diffuse0, fs_in.texcoord);
    const vec4 specular_map = u_is_sphere ? vec4(1.0f) : texture(u_material.specular0, fs_in.texcoord);

    const vec3 normal = normalize(fs_in.normal);

    vec3 out_color = calculate_directional_light(u_dir_light, normal, diffuse_map.rgb, specular_map.rgb);

    for (uint i = 0; i < u_point_lights_count; ++i) {
        out_color += calculate_point_light(u_point_lights[i], normal, diffuse_map.rgb, specular_map.rgb);
    }

    for (uint i = 0; i < u_spot_lights_count; ++i) {
        out_color += calculate_spot_light(u_spot_lights[i], normal, diffuse_map.rgb, specular_map.rgb);
    }

    frag_color = vec4(out_color, diffuse_map.a);
}