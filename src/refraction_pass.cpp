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
            QStringLiteral(":/effects/forceblur/shaders/vertex.vert"),
            QStringLiteral(":/effects/forceblur/shaders/refraction.frag"));

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
            QStringLiteral(":/effects/forceblur/shaders/contrast_rounded.vert"),
            QStringLiteral(":/effects/forceblur/shaders/refraction_rounded.frag"));

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
