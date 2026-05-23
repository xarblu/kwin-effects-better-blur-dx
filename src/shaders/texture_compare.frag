uniform sampler2D texUnitOld;
uniform sampler2D texUnitNew;

varying vec2 uv;

void main() {
    vec4 colorOld = texture2D(texUnitOld, uv);
    vec4 colorNew = texture2D(texUnitNew, uv);

    // discard (almost) identical pixels (using squared vec distance)
    vec4 colorDiff = colorOld - colorNew;
    if (dot(colorDiff, colorDiff) < 0.001) {
        discard;
    }

    // not discarded -> different
    gl_FragColor = vec4(1.0);
}
