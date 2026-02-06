#extension GL_OES_standard_derivatives : enable

#include "sdf.glsl"

uniform sampler2D texUnit;
uniform vec4 box;
uniform vec4 cornerRadius;

varying vec2 uv;
varying vec2 vertex;

void main(void)
{
    vec4 fragColor = texture2D(texUnit, uv);

    float f = sdfRoundedBox(vertex, box.xy, box.zw, cornerRadius);
    float df = fwidth(f);
    float inv_alpha = clamp(0.5 + f / df, 0.0, 1.0);
    fragColor *= inv_alpha;

    gl_FragColor = fragColor;
}
