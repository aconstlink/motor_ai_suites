#pragma once

namespace reference
{
    inline constexpr char const * vertex_shader = R"(
        #version 330 core
        layout(location=0) in vec3 ciPosition;
        uniform mat4 u_world;
        uniform mat4 u_view;
        uniform mat4 u_projection;
        void main() { gl_Position = u_projection * u_view * u_world * vec4(ciPosition, 1.0); }
    )" ;
    inline constexpr char const * fragment_shader = R"(
        #version 330 core
        uniform vec4 u_color;
        out vec4 out_color;
        void main() { out_color = u_color; }
    )" ;
}
