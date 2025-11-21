uniform float topCornerRadius;
uniform float bottomCornerRadius;
uniform float antialiasing;

uniform vec2 blurSize;
uniform float opacity;

vec4 roundedRectangle(vec2 fragCoord, vec3 texture)
{
    if (topCornerRadius == 0 && bottomCornerRadius == 0) {
        return vec4(texture, opacity);
    }

    vec2 halfblurSize = blurSize * 0.5;
    vec2 p = fragCoord - halfblurSize;
    float radius = 0.0;
    if ((fragCoord.y <= bottomCornerRadius)
        && (fragCoord.x <= bottomCornerRadius || fragCoord.x >= blurSize.x - bottomCornerRadius)) {
        radius = bottomCornerRadius;
        p.y -= radius;
    } else if ((fragCoord.y >= blurSize.y - topCornerRadius)
        && (fragCoord.x <= topCornerRadius || fragCoord.x >= blurSize.x - topCornerRadius)) {
        radius = topCornerRadius;
        p.y += radius;
    }
    float distance = length(max(abs(p) - (halfblurSize + vec2(0.0, radius)) + radius, 0.0)) - radius;

    float s = smoothstep(0.0, antialiasing, distance);
    return vec4(texture, mix(1.0, 0.0, s) * opacity);
}


uniform sampler2D texUnit;
uniform float offset;
uniform vec2 halfpixel;

varying vec2 uv;

void main(void)
{
    vec2 offsets[8] = vec2[](
        vec2(-halfpixel.x * 2.0, 0.0),
        vec2(-halfpixel.x, halfpixel.y),
        vec2(0.0, halfpixel.y * 2.0),
        vec2(halfpixel.x, halfpixel.y),
        vec2(halfpixel.x * 2.0, 0.0),
        vec2(halfpixel.x, -halfpixel.y),
        vec2(0.0, -halfpixel.y * 2.0),
        vec2(-halfpixel.x, -halfpixel.y)
    );
    float weights[8] = float[](1.0, 2.0, 1.0, 2.0, 1.0, 2.0, 1.0, 2.0);
    float weightSum = 12.0;
    vec4 sum = vec4(0, 0, 0, 0);

    for (int i = 0; i < 8; ++i) {
        vec2 off = offsets[i] * offset;
        sum += texture2D(texUnit, uv + off) * weights[i];
    }

    sum /= weightSum;

    gl_FragColor = roundedRectangle(uv * blurSize, sum.rgb);
}
