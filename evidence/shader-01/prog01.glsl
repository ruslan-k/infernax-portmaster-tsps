#version 310 es

layout(location = 0) in vec4 in_attr0;
layout(location = 1) in vec4 in_attr1;
layout(location = 0) out vec4 out_attr0;

void main()
{
    gl_Position = vec4(0.0, 0.0, 0.0, 1.0);
    out_attr0 = vec4(0.0, 0.0, 0.0, 1.0);
    gl_Position.x = in_attr0.x;
    gl_Position.y = in_attr0.y;
    out_attr0.x = in_attr1.x;
    out_attr0.y = in_attr1.y;
}

