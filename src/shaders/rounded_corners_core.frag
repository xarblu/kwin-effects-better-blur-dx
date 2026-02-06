#version 140

#include "sdf.glsl"

uniform sampler2D texUnit;
uniform vec4 box;
uniform vec4 cornerRadius;

in vec2 uv;
in vec2 vertex;

out vec4 fragColor;

void main(void)
{
    fragColor = texture(texUnit, uv);

    float f = sdfRoundedBox(vertex, box.xy, box.zw, cornerRadius);
    float df = fwidth(f);
    float inv_alpha = clamp(0.5 + f / df, 0.0, 1.0);
    fragColor *= inv_alpha;
}
