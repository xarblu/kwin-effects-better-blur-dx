#include <memory>

namespace KWin {
    class GLShader;
}

namespace BBDX {

class RefractionPass {
private:
    struct Rectangular {
        std::unique_ptr<KWin::GLShader> shader;
        int mvpMatrixLocation;
        int colorMatrixLocation;
        int offsetLocation;
        int halfpixelLocation;
    };

    struct Rounded {
        std::unique_ptr<KWin::GLShader> shader;
        int mvpMatrixLocation;
        int colorMatrixLocation;
        int offsetLocation;
        int halfpixelLocation;
        int boxLocation;
        int cornerRadiusLocation;
        int opacityLocation;
    };

    Rectangular m_rectangular;
    Rounded m_rounded;

public:
    /**
     * Loads required shaders and sets up shader uniformLocations
     */
    explicit RefractionPass();

    /**
     * Check if pass is ready i.e. all shaders loaded
     */
    bool ready() { return m_rectangular.shader && m_rounded.shader; }
};

} // namespace BBDX

