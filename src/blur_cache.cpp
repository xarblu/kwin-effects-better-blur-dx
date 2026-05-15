#include "blur_cache.hpp"

#include "blur.h"
#include "kwin_version.hpp"
#include "utils.h"

#include <core/renderviewport.h>
#include <epoxy/gl.h>
#include <epoxy/gl_generated.h>
#include <opengl/eglcontext.h>
#include <opengl/glframebuffer.h>
#include <opengl/glshadermanager.h>
#include <qloggingcategory.h>
#include <qobject.h>

#if KWIN_VERSION < KWIN_VERSION_CODE(6, 5, 80)
#  include "kwin_compat_6_5.hpp"
#else
#  include <core/rect.h>
#endif

#include <QLoggingCategory>
#include <QtNumeric>
#include <opengl/gltexture.h>

Q_LOGGING_CATEGORY(BLUR_CACHE, "kwin_effect_better_blur_dx.blur_cache", QtInfoMsg)

bool BBDX::BlurCacheData::invalidate(QStringView reason) {
    if (!valid) {
        return false;
    }

    QString windowClass;
    if (w) [[likely]] {
        windowClass = w->windowClass();
    } else {
        windowClass = QStringLiteral("unknown window");
    }

    qCDebug(BLUR_CACHE) << BBDX::LOG_PREFIX << "Cache invalidated:" << windowClass << "\n"
                        << "Hits:"   << hits << "\n"
                        << "Reason:" << reason;

    valid = false;
    hits = 0;

    return true;
}

BBDX::BlurCache::BlurCache() {
    m_textureComparePass.shader = KWin::ShaderManager::instance()->generateShaderFromFile(KWin::ShaderTrait::MapTexture,
                                                                           QStringLiteral(":/effects/better_blur_dx/shaders/vertex.vert"),
                                                                           QStringLiteral(":/effects/better_blur_dx/shaders/texture_compare.frag"));
    if (!m_textureComparePass.shader) {
        qCWarning(BLUR_CACHE) << BBDX::LOG_PREFIX << "Failed to load texture compare pass shader";
        return;
    } else {
        m_textureComparePass.mvpMatrixLocation = m_textureComparePass.shader->uniformLocation("modelViewProjectionMatrix");
        m_textureComparePass.texUnitOldLocation = m_textureComparePass.shader->uniformLocation("texUnitOld");
        m_textureComparePass.texUnitNewLocation = m_textureComparePass.shader->uniformLocation("texUnitNew");
    }

    m_texturePass.shader = KWin::ShaderManager::instance()->generateShaderFromFile(KWin::ShaderTrait::MapTexture,
                                                                           QStringLiteral(":/effects/better_blur_dx/shaders/vertex.vert"),
                                                                           QStringLiteral(":/effects/better_blur_dx/shaders/texture.frag"));
    if (!m_texturePass.shader) {
        qCWarning(BLUR_CACHE) << BBDX::LOG_PREFIX << "Failed to load texture pass shader";
        return;
    } else {
        m_texturePass.mvpMatrixLocation = m_texturePass.shader->uniformLocation("modelViewProjectionMatrix");
    }
}

void BBDX::BlurCache::updateBlurCacheDataBuffers(KWin::BlurRenderData &renderInfo, const KWin::Rect &scaledBackgroundRect, GLenum textureFormat) const {
    if (!renderInfo.cache.texture || renderInfo.cache.texture->size() != scaledBackgroundRect.size() || renderInfo.cache.texture->internalFormat() != textureFormat) {
        glClearColor(0, 0, 0, 0);
        auto texture = KWin::GLTexture::allocate(textureFormat, scaledBackgroundRect.size());
        if (!texture) {
            qCWarning(BLUR_CACHE) << BBDX::LOG_PREFIX << "Failed to allocate an offscreen texture";
            return;
        }
        texture->setFilter(GL_LINEAR);
        texture->setWrapMode(GL_CLAMP_TO_EDGE);

        auto framebuffer = std::make_unique<KWin::GLFramebuffer>(texture.get());
        if (!framebuffer->valid()) {
            qCWarning(BLUR_CACHE) << BBDX::LOG_PREFIX << "Failed to create an offscreen framebuffer";
            return;
        }
#if defined(BETTERBLUR_X11)
        auto *context = KWin::OpenGlContext::currentContext();
#else
        auto *context = KWin::EglContext::currentContext();
#endif
        context->pushFramebuffer(framebuffer.get());
        glClear(GL_COLOR_BUFFER_BIT);
        context->popFramebuffer();
        renderInfo.cache.texture = std::move(texture);
        renderInfo.cache.framebuffer = std::move(framebuffer);
    }
}

void BBDX::BlurCache::maybeInvalidateCache(KWin::BlurRenderData &renderInfo,
                                           qreal opacity,
                                           KWin::GLVertexBuffer *vbo) const {
    auto &cacheData = renderInfo.cache;
    if (!cacheData.opacity.has_value() || !qFuzzyCompare(cacheData.opacity.value(), opacity)) {
        cacheData.opacity = opacity;
        cacheData.invalidate(QStringLiteral("Opacity changed"));
    }

    // Fast path in case we invalidated earlier.
    // Note that this should still come after opacity checks et al
    // because those update data.
    if (!cacheData.valid) {
        return;
    }

    KWin::GLTexture *prevBlitTexture = cacheData.prevBlitTexture.get();
    KWin::GLFramebuffer *blitFramebuffer = renderInfo.framebuffers[0].get();
    KWin::GLTexture *blitTexture = blitFramebuffer->colorAttachment();

    // previous blit texture is definitely different
    if (!prevBlitTexture) {
        cacheData.invalidate(QStringLiteral("No prevBlitTexture"));
        return;
    }

    if (prevBlitTexture->size() != blitTexture->size()) {
        cacheData.invalidate(QStringLiteral("Blit texture size mismatch"));
        return;
    }

    if (prevBlitTexture->internalFormat() != blitTexture->internalFormat()) {
        cacheData.invalidate(QStringLiteral("Blit texture format mismatch"));
        return;
    }

    // check if textures differ on the pixel level
    // we'll just (ab)use the provided framebuffer for this
    // as it *should* always be correct
    KWin::ShaderManager::instance()->pushShader(m_textureComparePass.shader.get());
    KWin::GLFramebuffer::pushFramebuffer(blitFramebuffer);

    QMatrix4x4 projectionMatrix;
    projectionMatrix.ortho(QRectF(0.0, 0.0, blitTexture->width(), blitTexture->height()));

    m_textureComparePass.shader->setUniform(m_textureComparePass.mvpMatrixLocation, projectionMatrix);

    m_textureComparePass.shader->setUniform(m_textureComparePass.texUnitOldLocation, 0);
    glActiveTexture(GL_TEXTURE0);
    prevBlitTexture->bind();

    m_textureComparePass.shader->setUniform(m_textureComparePass.texUnitNewLocation, 1);
    glActiveTexture(GL_TEXTURE1);
    blitTexture->bind();

    GLuint query;
    glGenQueries(1, &query);

    // don't acctually draw anything
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);

    // check for non-discarded pixels
    // GL_ANY_SAMPLES_PASSED_CONSERVATIVE is supposedly faster but
    // implementation dependent, may have false positives (meaning cache invalidation when not needed)
    // and needs new-ish OpenGL 4.3 (https://registry.khronos.org/OpenGL-Refpages/gl4/html/glBeginQuery.xhtml)
    // so let's just use the slightly slower GL_ANY_SAMPLES_PASSED (OpenGL 3.3)
    glBeginQuery(GL_ANY_SAMPLES_PASSED, query);
    
    // if the query isn't available just invalidate, not much we can do here
    if (glGetError() == GL_INVALID_ENUM) [[unlikely]] {
        qCWarning(BLUR_CACHE) << "OpenGL error: GL_ANY_SAMPLES_PASSED query not available";
        cacheData.invalidate(QStringLiteral("GL_ANY_SAMPLES_PASSED query not available - assuming blit pixel difference"));
        glEndQuery(GL_ANY_SAMPLES_PASSED);
        goto cleanup;
    }

    // perform query
    vbo->draw(GL_TRIANGLES, 0, 6);
    glEndQuery(GL_ANY_SAMPLES_PASSED);

    // await query and check
    GLuint anyPixelsDifferent;
    glGetQueryObjectuiv(query, GL_QUERY_RESULT, &anyPixelsDifferent);
    if (anyPixelsDifferent == GL_TRUE) {
        cacheData.invalidate(QStringLiteral("Blit pixel difference"));
    }

cleanup:
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDeleteQueries(1, &query);
    glActiveTexture(GL_TEXTURE0);

    KWin::GLFramebuffer::popFramebuffer();
    KWin::ShaderManager::instance()->popShader();
}

void BBDX::BlurCache::setupVBO(const KWin::Rect &scaledBackgroundRect, std::span<KWin::GLVertex2D> &map, size_t &vboIndex) const {
    // The geometry used for the cache, in logical pixels
    // but scaled to what would be drawn on the device.
    {
        const QRectF localRect = QRectF(0, 0, scaledBackgroundRect.width(), scaledBackgroundRect.height());

        const float x0 = localRect.left();
        const float y0 = localRect.top();
        const float x1 = localRect.right();
        const float y1 = localRect.bottom();

        const float u0 = x0 / scaledBackgroundRect.width();
        const float v0 = 1.0f - y0 / scaledBackgroundRect.height();
        const float u1 = x1 / scaledBackgroundRect.width();
        const float v1 = 1.0f - y1 / scaledBackgroundRect.height();

        // first triangle
        map[vboIndex++] = KWin::GLVertex2D{
            .position = QVector2D(x0, y0),
            .texcoord = QVector2D(u0, v0),
        };
        map[vboIndex++] = KWin::GLVertex2D{
            .position = QVector2D(x1, y1),
            .texcoord = QVector2D(u1, v1),
        };
        map[vboIndex++] = KWin::GLVertex2D{
            .position = QVector2D(x0, y1),
            .texcoord = QVector2D(u0, v1),
        };

        // second triangle
        map[vboIndex++] = KWin::GLVertex2D{
            .position = QVector2D(x0, y0),
            .texcoord = QVector2D(u0, v0),
        };
        map[vboIndex++] = KWin::GLVertex2D{
            .position = QVector2D(x1, y0),
            .texcoord = QVector2D(u1, v0),
        };
        map[vboIndex++] = KWin::GLVertex2D{
            .position = QVector2D(x1, y1),
            .texcoord = QVector2D(u1, v1),
        };
    }
};

void BBDX::BlurCache::drawCached(const KWin::Rect &scaledBackgroundRect, const KWin::RenderViewport &viewport, KWin::BlurRenderData &renderInfo, KWin::GLVertexBuffer *vbo, const int vertexCount, const float modulation) const {
    KWin::ShaderManager::instance()->pushShader(m_texturePass.shader.get());
    
    QMatrix4x4 projectionMatrix = viewport.projectionMatrix();
    projectionMatrix.translate(scaledBackgroundRect.x(), scaledBackgroundRect.y());

    const auto &read = renderInfo.cache.framebuffer->colorAttachment();

    m_texturePass.shader->setUniform(m_texturePass.mvpMatrixLocation, projectionMatrix);
    read->bind();

    if (modulation < 1.0) {
        glEnable(GL_BLEND);
        glBlendColor(0, 0, 0, modulation);
        glBlendFunc(GL_CONSTANT_ALPHA, GL_ONE_MINUS_CONSTANT_ALPHA);
    }

    vbo->draw(GL_TRIANGLES, 12, vertexCount);

    if (modulation < 1.0) {
        glDisable(GL_BLEND);
    }

    KWin::ShaderManager::instance()->popShader();

    // if we drew it, it has to valid, right?
    if (!renderInfo.cache.valid) {
        renderInfo.cache.valid = true;

        // clone current blit for next draw
        renderInfo.cache.prevBlitTexture = cloneBlitTexture(renderInfo);
    } else {
        renderInfo.cache.hits += 1;
    }
}

void BBDX::BlurCache::drawToCache(const KWin::BlurRenderData &renderInfo, KWin::GLVertexBuffer *vbo) const {
    KWin::GLFramebuffer::pushFramebuffer(renderInfo.cache.framebuffer.get());
    vbo->draw(GL_TRIANGLES, 6, 6);
    KWin::GLFramebuffer::popFramebuffer();
}

std::unique_ptr<KWin::GLTexture> BBDX::BlurCache::cloneBlitTexture(KWin::BlurRenderData &renderInfo) const {
    KWin::GLFramebuffer *sourceBuffer = renderInfo.framebuffers[0].get();
    KWin::GLTexture *sourceTexture = sourceBuffer->colorAttachment();

    auto texture = KWin::GLTexture::allocate(sourceTexture->internalFormat(), sourceTexture->size());
    if (!texture) {
        qCWarning(BLUR_CACHE) << BBDX::LOG_PREFIX << "Failed to allocate an offscreen texture";
        return nullptr;
    }
    texture->setFilter(GL_LINEAR);
    texture->setWrapMode(GL_CLAMP_TO_EDGE);

    auto framebuffer = std::make_unique<KWin::GLFramebuffer>(texture.get());
    if (!framebuffer->valid()) {
        qCWarning(BLUR_CACHE) << BBDX::LOG_PREFIX << "Failed to create an offscreen framebuffer";
        return nullptr;
    }

    KWin::GLFramebuffer::pushFramebuffer(sourceBuffer);
    framebuffer->blitFromFramebuffer();
    KWin::GLFramebuffer::popFramebuffer();

    return texture;
}
