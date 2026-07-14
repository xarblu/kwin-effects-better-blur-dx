#version 140

uniform sampler2D texUnit;
uniform float modulation;

in vec2 uv;

out vec4 fragColor;

void main(void)
{
    vec4 color = texture(texUnit, uv);
    fragColor = vec4(color.rgb, color.a * modulation);
}
