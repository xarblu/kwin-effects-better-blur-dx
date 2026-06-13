// KWin dumbs this down to OpenGL 3.1 (#version 140) / OpenGL ES 3.0 (#version 300)
// so we explicitly require extensions

// SSBO blocks
#extension GL_ARB_shader_storage_buffer_object : require
// binding = 2 syntax
#extension GL_ARB_shading_language_420pack : require

// glQuery glue for texture_compare_and_update.comp

// bind to the exact same SSBO slot as the compute shader
layout(std140, binding = 2) buffer AtomicCounterBuffer {
    uint globalChangeCount;
};

void main() {
    // compute shader didn't find a difference
    if (globalChangeCount == 0u) {
        discard; 
    }
    
    // dummy pass
    gl_FragColor = vec4(1.0); 
}
