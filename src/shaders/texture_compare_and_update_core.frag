#version 430

// glQuery glue for texture_compare_and_update.comp

// bind to the exact same SSBO slot as the compute shader
layout(std430, binding = 2) buffer AtomicCounterBuffer {
    uint globalChangeCount;
};

out vec4 FragColor;

void main() {
    // compute shader didn't find a meaningful difference
    if (globalChangeCount < 5u) {
        discard; 
    }
    
    // dummy pass
    FragColor = vec4(1.0); 
}
