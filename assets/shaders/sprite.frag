#version 450

layout(set = 2, binding = 0) uniform sampler2D atlas;
layout(std140, set = 3, binding = 0) uniform Perception {
    vec4 red;
    vec4 green;
    vec4 blue;
};

// Match SDL 3.4's textured vertex shader outputs.
layout(location = 0) in vec4 color;
layout(location = 1) in vec2 uv;
layout(location = 0) out vec4 outputColor;

void main() {
    vec4 sampled = texture(atlas, uv);
    vec4 rgb = vec4(sampled.rgb, 1.0);
    vec3 transformed = clamp(vec3(dot(rgb, red), dot(rgb, green), dot(rgb, blue)), 0.0, 1.0);
    outputColor = vec4(transformed, sampled.a) * color;
}
