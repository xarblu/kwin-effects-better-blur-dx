uniform sampler2D texUnit;
uniform mat4 colorMatrix;
uniform float offset;
uniform vec2 halfpixel;
uniform vec3 micaTint;
uniform float micaStrength;
uniform float micaLuminosity;

varying vec2 uv;

// The mica effect is the wallpaper's luminosity blended onto
// the tint. The values of .3, .59, .11 are from the spec itself for blend model
float bbdxLum(vec3 c)
{
    return dot(c, vec3(0.3, 0.59, 0.11));
}

vec3 bbdxClipColor(vec3 c)
{
    float l = bbdxLum(c);
    float n = min(min(c.r, c.g), c.b);
    float x = max(max(c.r, c.g), c.b);

    // using max calls here and below to guard against a fully achromatic pixel
    // as that'd be a divide by zero
    if (n < 0.0) {
        c = l + ((c - l) * l) / max(l - n, 1e-5);
    }
    if (x > 1.0) {
        c = l + ((c - l) * (1.0 - l)) / max(x - l, 1e-5);
    }

    return c;
}

vec3 bbdxSetLum(vec3 c, float l)
{
    return bbdxClipColor(c + (l - bbdxLum(c)));
}

void main(void)
{
    vec4 sum = texture2D(texUnit, uv + vec2(-halfpixel.x * 2.0, 0.0) * offset);
    sum += texture2D(texUnit, uv + vec2(-halfpixel.x, halfpixel.y) * offset) * 2.0;
    sum += texture2D(texUnit, uv + vec2(0.0, halfpixel.y * 2.0) * offset);
    sum += texture2D(texUnit, uv + vec2(halfpixel.x, halfpixel.y) * offset) * 2.0;
    sum += texture2D(texUnit, uv + vec2(halfpixel.x * 2.0, 0.0) * offset);
    sum += texture2D(texUnit, uv + vec2(halfpixel.x, -halfpixel.y) * offset) * 2.0;
    sum += texture2D(texUnit, uv + vec2(0.0, -halfpixel.y * 2.0) * offset);
    sum += texture2D(texUnit, uv + vec2(-halfpixel.x, -halfpixel.y) * offset) * 2.0;

    vec4 color = (sum / 12.0) * colorMatrix;

    if (micaStrength > 0.0) {
        // Attach the surface's lightness to the tint instead of taking it
        // from the backdrop itself to match how Fluent does it
        float micaL = mix(bbdxLum(micaTint), bbdxLum(color.rgb), micaLuminosity);
        color.rgb = mix(color.rgb, bbdxSetLum(micaTint, micaL), micaStrength);
    }

    gl_FragColor = color;
}
