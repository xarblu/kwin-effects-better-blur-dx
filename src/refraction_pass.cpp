#include "blurconfig.h"
#include "refraction_pass.hpp"
#include "utils.h"

#include <opengl/glshader.h>
#include <opengl/glshadermanager.h>

#include <QLoggingCategory>
#include <qloggingcategory.h>

Q_LOGGING_CATEGORY(REFRACTION_PASS, "kwin_effect_better_blur_dx.refraction_pass", QtWarningMsg)

BBDX::RefractionPass::RefractionPass() {
    // The vertex shaders should always be the one of the
    // respective contrast pass.
    // The refraction uses a modified version of
    // the contrast fragment shader.

    m_rectangular.shader = KWin::ShaderManager::instance()->generateShaderFromFile(
            KWin::ShaderTrait::MapTexture,
            QStringLiteral(":/effects/better_blur_dx/shaders/vertex.vert"),
            QStringLiteral(":/effects/better_blur_dx/shaders/refraction.frag"));

    if (!m_rectangular.shader) {
        qCWarning(REFRACTION_PASS) << BBDX::LOG_PREFIX << "Failed to load refraction pass shader";
        return;
    } else {
        m_rectangular.mvpMatrixLocation = m_rectangular.shader->uniformLocation("modelViewProjectionMatrix");
        m_rectangular.colorMatrixLocation = m_rectangular.shader->uniformLocation("colorMatrix");
        m_rectangular.offsetLocation = m_rectangular.shader->uniformLocation("offset");
        m_rectangular.halfpixelLocation = m_rectangular.shader->uniformLocation("halfpixel");
    }

    m_rounded.shader = KWin::ShaderManager::instance()->generateShaderFromFile(
            KWin::ShaderTrait::MapTexture,
            QStringLiteral(":/effects/better_blur_dx/shaders/contrast_rounded.vert"),
            QStringLiteral(":/effects/better_blur_dx/shaders/refraction_rounded.frag"));

    if (!m_rounded.shader) {
        qCWarning(REFRACTION_PASS) << BBDX::LOG_PREFIX << "Failed to load refraction pass shader";
        return;
    } else {
        m_rounded.mvpMatrixLocation = m_rounded.shader->uniformLocation("modelViewProjectionMatrix");
        m_rounded.colorMatrixLocation = m_rounded.shader->uniformLocation("colorMatrix");
        m_rounded.offsetLocation = m_rounded.shader->uniformLocation("offset");
        m_rounded.halfpixelLocation = m_rounded.shader->uniformLocation("halfpixel");
        m_rounded.boxLocation = m_rounded.shader->uniformLocation("box");
        m_rounded.cornerRadiusLocation = m_rounded.shader->uniformLocation("cornerRadius");
        m_rounded.opacityLocation = m_rounded.shader->uniformLocation("opacity");
    }
}

void BBDX::RefractionPass::reconfigure() {
    auto config = KWin::BlurConfig::self();

    if (!config) {
        qCWarning(REFRACTION_PASS) << BBDX::LOG_PREFIX
                                   << "RefractionPass::reconfigure() called before BlurConfig::read()";
        return;
    }

    m_normalPow = config->refractionNormalPow();
    m_strength = config->refractionStrength();
    m_edgeSize = config->refractionEdgeSize();
    m_cornerRadius = config->refractionCornerRadius();
    m_RGBFringing = config->refractionRGBFringing();
    m_textureRepeatMode = config->refractionTextureRepeatMode();
    m_mode = config->refractionMode();
}

bool BBDX::RefractionPass::pushShaderRounded() const {
    if (!enabled())
        return false;

    KWin::ShaderManager::instance()->pushShader(m_rounded.shader.get());

    return true;
}

bool BBDX::RefractionPass::pushShaderRectangular() const {
    if (!enabled())
        return false;

    KWin::ShaderManager::instance()->pushShader(m_rectangular.shader.get());

    return true;
}

bool BBDX::RefractionPass::setParametersRounded(const QMatrix4x4 &projectionMatrix,
                                         const QMatrix4x4 &colorMatrix,
                                         const QVector2D &halfpixel,
                                         const float offset,
                                         const QVector4D &box,
                                         const QVector4D &cornerRadius,
                                         const qreal opacity) const {

    if (!enabled())
        return false;

    m_rounded.shader->setUniform(m_rounded.mvpMatrixLocation, projectionMatrix);
    m_rounded.shader->setUniform(m_rounded.colorMatrixLocation, colorMatrix);
    m_rounded.shader->setUniform(m_rounded.halfpixelLocation, halfpixel);
    m_rounded.shader->setUniform(m_rounded.offsetLocation, offset);
    m_rounded.shader->setUniform(m_rounded.boxLocation, box);
    m_rounded.shader->setUniform(m_rounded.cornerRadiusLocation, cornerRadius);
    m_rounded.shader->setUniform(m_rounded.opacityLocation, opacity);

    return true;
}

bool BBDX::RefractionPass::setParametersRectangular(const QMatrix4x4 &projectionMatrix,
                                         const QMatrix4x4 &colorMatrix,
                                         const QVector2D &halfpixel,
                                         const float offset) const {

    if (!enabled())
        return false;

    m_rectangular.shader->setUniform(m_rectangular.mvpMatrixLocation, projectionMatrix);
    m_rectangular.shader->setUniform(m_rectangular.colorMatrixLocation, colorMatrix);
    m_rectangular.shader->setUniform(m_rectangular.halfpixelLocation, halfpixel);
    m_rectangular.shader->setUniform(m_rectangular.offsetLocation, offset);

    return true;
}
