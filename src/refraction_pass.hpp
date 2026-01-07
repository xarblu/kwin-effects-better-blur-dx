#pragma once

#include <QMatrix4x4>
#include <QVector2D>

#include <memory>
#include <qvectornd.h>

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

    Rectangular m_rectangular{};
    Rounded m_rounded{};

    bool m_enabled{false};

    // user settings
    qreal m_normalPow{};
    qreal m_strength{};
    qreal m_edgeSize{};
    qreal m_cornerRadius{};
    qreal m_RGBFringing{};
    int m_textureRepeatMode{};
    int m_mode{};

public:
    /**
     * Loads required shaders and sets up shader uniformLocations
     */
    explicit RefractionPass();

    /**
     * reconfigure from BlurConfig
     */
    void reconfigure();

    /**
     * Check if pass is ready i.e. all shaders loaded
     */
    bool ready() const { return m_rectangular.shader && m_rounded.shader; }

    /**
     * Check if refraction pass is enabled
     */
    bool enabled() const { return m_enabled; }

    /**
     * Push respective shader to the ShaderManager
     *
     * returns false if refraction is disabled
     */
    bool pushShaderRounded() const;
    bool pushShaderRectangular() const;

    /**
     * Set GLSL parameters, rounded version
     *
     * returns false if refraction is disabled
     */
    bool setParametersRounded(const QMatrix4x4 &projectionMatrix,
                              const QMatrix4x4 &colorMatrix,
                              const QVector2D &halfpixel,
                              const float offset,
                              const QVector4D &box,
                              const QVector4D &cornerRadius,
                              const qreal opacity) const;

    /**
     * Set GLSL parameters, rectangular version
     *
     * returns false if refraction is disabled
     */
    bool setParametersRectangular(const QMatrix4x4 &projectionMatrix,
                                  const QMatrix4x4 &colorMatrix,
                                  const QVector2D &halfpixel,
                                  const float offset) const;
};

} // namespace BBDX

