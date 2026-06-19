#pragma once

#include "kwin_compat.hpp"

#include <effect/effectwindow.h>
#include <opengl/glshadermanager.h>
#include <opengl/glshader.h>
#include <opengl/gltexture.h>

#include <epoxy/gl.h>

#include <memory>
#include <unordered_map>
#include <array>

namespace BBDX {

struct BlurCachePaintData;

class TextureComparer {
public:
    /**
     * Per Window (BlurCacheLRU) data
     */
    class WindowData {
        /**
         * Amount of slots per window
         */
        static constexpr int SLOTS{3};

        /**
         * Last returned slot
         */
        int m_lastSlot{0};

        /**
         * SSBO counting changed blocks
         */
        std::array<GLuint, SLOTS> m_counterBuffers{};

        /**
         * GL query object used for conditional render
         */
        std::array<GLuint, SLOTS> m_queries{};

        /**
         * Use create()
         */
        WindowData() = default;

    public:
        /**
         * Create WindowData
         * nullptr on error
         */
        static std::unique_ptr<WindowData> create();

        /**
         * Cleanup GL resources
         */
        ~WindowData();

        /**
         * Get a matching {counterBuffer, query} tuple
         *
         * returns nullopt if all query objects are currently busy
         */
        std::optional<std::pair<GLuint, GLuint>> getSlot();
    };

    // the Rect struct as used in the compute shader
    struct ComputeShaderRect {
        GLint x;
        GLint y;
        GLint width;
        GLint height;
    };

private:
    // a compute shader instance
    // because we need one for each texture format
    struct ComputeShader {
        GLuint program{0};
        GLuint dirtyRegionBuffer{};
        GLint dirtyRegionRectCountLocation{};
        GLint dirtyRegionBoundingBoxLocation{};

        ~ComputeShader() {
            if (program > 0) {
                glDeleteProgram(program);
            }

            if (dirtyRegionBuffer > 0) {
                glDeleteBuffers(1, &dirtyRegionBuffer);
            }
        }
    };

    // compute shaders - we need to handle this ourselves :p
    // one for each format we encountered
    std::unordered_map<GLenum, std::unique_ptr<ComputeShader>> m_computeShaders{};

    // regular vert+frag so let KWin handle it
    std::unique_ptr<KWin::GLShader> m_glueShader{nullptr};

    TextureComparer() = default;

    /**
     * Build a compute shader for the given format
     * nullptr on error
     */
    static std::unique_ptr<ComputeShader> buildComputeShader(GLenum textureFormat);

public:
    /**
     * Create a new TextureComparer
     * nullptr on error
     */
    static std::unique_ptr<TextureComparer> create();

    /**
     * No copying
     */
    TextureComparer(TextureComparer &other) = delete;
    TextureComparer& operator=(TextureComparer &other) = delete;

    /**
     * Compare and update cachedBlit with freshBlit
     * within the localDirtyRegion (in GL coords)
     *
     * windowDataSlot is a pair as returned by WindowData::getSlot()
     *
     * The EffectWindow is optional and only used
     * for extra logging in the debug build (BBDX_DEBUG)
     *
     * The result of the comparison can be found using the
     * query object returned by queryObject()
     *
     * Returns false on GL error; caller must NOT issue
     * glBeginConditionalRender on the slot's query in that case.
     */
    bool compareAndUpdate(const std::pair<GLuint, GLuint> &windowDataSlot, KWin::GLTexture *freshBlit, KWin::GLTexture *cachedBlit, const BlurCachePaintData &paintData);
};

}
