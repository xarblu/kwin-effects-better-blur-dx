#version 140

uniform sampler2D texUnitOld;
uniform sampler2D texUnitNew;

in vec2 uv;

out vec4 fragColor;

void main() {
    // Cast physical screen fragment coordinate to integer
    ivec2 physCoord = ivec2(gl_FragCoord.xy);

    // Read the exact raw bytes from both FBOs
    vec4 oldColor = texelFetch(texUnitOld, physCoord, 0);
    vec4 newColor = texelFetch(texUnitNew, physCoord, 0);

    vec4 diff = abs(oldColor - newColor);
    if (all(lessThan(diff, vec4(0.01)))) {
        discard;
    }
}
