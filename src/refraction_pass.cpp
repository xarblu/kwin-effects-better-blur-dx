#include "blurconfig.h"
#include "refraction_pass.hpp"
#include "utils.h"

#include <memory>
#include <opengl/glshader.h>
#include <opengl/glshadermanager.h>

#include <QLoggingCategory>
#include <QRect>

#include <algorithm>

Q_LOGGING_CATEGORY(REFRACTION_PASS, "kwin_effect_better_blur_dx.refraction_pass", QtInfoMsg)

std::unique_ptr<BBDX::RefractionPass> BBDX::RefractionPass::create() {
    // The vertex shaders should always be the one of the
    // respective contrast pass.
    // The refraction uses a modified version of
    // the contrast fragment shader.

    std::unique_ptr<RefractionPass> pass{new RefractionPass};

    pass->m_shader = KWin::ShaderManager::instance()->generateShaderFromFile(
        KWin::ShaderTrait::MapTexture,
        BBDX::shaderFilePath(":/effects/better_blur_dx/shaders/vertex.vert"),
        BBDX::shaderFilePath(":/effects/better_blur_dx/shaders/refraction.frag")
    );

    if (!pass->m_shader) {
        qCWarning(REFRACTION_PASS) << BBDX::LOG_PREFIX << "Failed to load refraction pass shader";
        return nullptr;
    } else {
        // contrast parameters
        pass->m_mvpMatrixLocation = pass->m_shader->uniformLocation("modelViewProjectionMatrix");
        pass->m_colorMatrixLocation = pass->m_shader->uniformLocation("colorMatrix");
        pass->m_offsetLocation = pass->m_shader->uniformLocation("offset");
        pass->m_halfpixelLocation = pass->m_shader->uniformLocation("halfpixel");
        // refraction parameters
        pass->m_refractionRectSizeLocation = pass->m_shader->uniformLocation("refractionRectSize");
        pass->m_refractionEdgeSizePixelsLocation = pass->m_shader->uniformLocation("refractionEdgeSizePixels");
        pass->m_refractionCornerRadiusPixelsLocation = pass->m_shader->uniformLocation("refractionCornerRadiusPixels");
        pass->m_refractionStrengthLocation = pass->m_shader->uniformLocation("refractionStrength");
        pass->m_refractionNormalPowLocation = pass->m_shader->uniformLocation("refractionNormalPow");
        pass->m_refractionRGBFringingLocation = pass->m_shader->uniformLocation("refractionRGBFringing");
        pass->m_refractionTextureRepeatModeLocation = pass->m_shader->uniformLocation("refractionTextureRepeatMode");
        pass->m_refractionModeLocation = pass->m_shader->uniformLocation("refractionMode");
        pass->m_micaTintLocation = pass->m_shader->uniformLocation("micaTint");
        pass->m_micaStrengthLocation = pass->m_shader->uniformLocation("micaStrength");
        pass->m_micaLuminosityLocation = pass->m_shader->uniformLocation("micaLuminosity");
    }

    return pass;
}

void BBDX::RefractionPass::reconfigure() {
    auto config = BBDX::BlurConfig::self();

    if (!config) {
        qCWarning(REFRACTION_PASS) << BBDX::LOG_PREFIX
                                   << "RefractionPass::reconfigure() called before BlurConfig::read()";
        return;
    }

    // mark enabled if strength > 0
    m_enabled = config->refractionStrength() > 0;

    // scaled up by 10.0
    constexpr double scaleEdgeSizePixels{10.0};
    m_edgeSizePixels = static_cast<double>(config->refractionEdgeSize()) * scaleEdgeSizePixels;

    // snapped to nearest step
    constexpr double maxCornerRadiusPixels{200.0};
    constexpr double steps{30.0};
    constexpr double stepSize{maxCornerRadiusPixels / steps}; // ~6.6667, matching step size from blur_config.ui
    const double snapped{std::round(static_cast<double>(config->refractionCornerRadius()) / stepSize) * stepSize};
    m_cornerRadiusPixels = snapped;

    // expects range 0.0-1.0
    // max value from blur_config.ui
    constexpr double maxStrength{30.0};
    m_strength = static_cast<double>(config->refractionStrength()) / maxStrength;

    // XXX: why scaled down?
    constexpr double scaleNormalPow{0.5};
    m_normalPow = static_cast<double>(config->refractionNormalPow()) * scaleNormalPow;

    // expects range 0.0-1.0
    // max value from blur_config.ui
    constexpr double maxRGBFringing{30.0};
    m_RGBFringing = static_cast<double>(config->refractionRGBFringing()) / maxRGBFringing;

    // integer mode selectors
    m_textureRepeatMode = config->refractionTextureRepeatMode();
    m_mode = config->refractionMode();
}

bool BBDX::RefractionPass::pushShader() const {
    if (!enabled())
        return false;

    KWin::ShaderManager::instance()->pushShader(m_shader.get());

    return true;
}


bool BBDX::RefractionPass::setParameters(const QMatrix4x4 &projectionMatrix,
                                         const QMatrix4x4 &colorMatrix,
                                         const QVector2D &halfpixel,
                                         const float offset,
                                         const QRect &scaledBackgroundRect,
                                         const QVector3D &micaTint,
                                         const float micaStrength,
                                         const float micaLuminosity) const {
    if (!enabled())
        return false;

    // contrast parameters
    m_shader->setUniform(m_mvpMatrixLocation, projectionMatrix);
    m_shader->setUniform(m_colorMatrixLocation, colorMatrix);
    m_shader->setUniform(m_halfpixelLocation, halfpixel);
    m_shader->setUniform(m_offsetLocation, offset);
    // refraction parameters
    m_shader->setUniform(m_refractionRectSizeLocation,
                                     QVector2D(scaledBackgroundRect.width(), scaledBackgroundRect.height()));
    m_shader->setUniform(m_refractionEdgeSizePixelsLocation,
                                     std::min(static_cast<float>(m_edgeSizePixels),
                                              static_cast<float>(std::min(scaledBackgroundRect.width() / 2,
                                                                          scaledBackgroundRect.height() / 2))));
    m_shader->setUniform(m_refractionCornerRadiusPixelsLocation, static_cast<float>(m_cornerRadiusPixels));
    m_shader->setUniform(m_refractionStrengthLocation, static_cast<float>(m_strength));
    m_shader->setUniform(m_refractionNormalPowLocation, static_cast<float>(m_normalPow));
    m_shader->setUniform(m_refractionRGBFringingLocation, static_cast<float>(m_RGBFringing));
    m_shader->setUniform(m_refractionTextureRepeatModeLocation, m_textureRepeatMode);
    m_shader->setUniform(m_refractionModeLocation, m_mode);
    // mica parameters
    m_shader->setUniform(m_micaTintLocation, micaTint);
    m_shader->setUniform(m_micaStrengthLocation, micaStrength);
    m_shader->setUniform(m_micaLuminosityLocation, micaLuminosity);

    return true;
}
