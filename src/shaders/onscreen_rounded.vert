uniform mat4 modelViewProjectionMatrix;

attribute vec2 position;
attribute vec2 texcoord;

varying vec2 uv;
varying vec2 vertex;

void main(void)
{
    gl_Position = modelViewProjectionMatrix * vec4(position, 0.0, 1.0);
    uv = texcoord;
    vertex = position;
}
