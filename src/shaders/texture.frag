uniform sampler2D texUnit;
uniform float modulation;

varying vec2 uv;

void main(void)
{
    vec4 color = texture2D(texUnit, uv);
    gl_FragColor = vec4(color.rgb, color.a * modulation);
}
