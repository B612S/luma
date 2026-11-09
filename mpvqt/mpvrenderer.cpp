/*
 * SPDX-FileCopyrightText: 2023 George Florea Bănuș <georgefb899@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL
 *
 * Modified for Luma, relative to upstream mpvqt v1.2.1:
 *   - Wayland-only display setup: the X11 (xcb) branch is removed, platform
 *     names starting with "wayland" are accepted, and the native interface is
 *     null-checked.
 *   - Clear the framebuffer to black and skip rendering while casting or while
 *     video output is disabled in MpvItem.
 *   - Add null guards for the item, the resource manager, the render context
 *     and the framebuffer object.
 *   - Pass MPV_RENDER_PARAM_BLOCK_FOR_TARGET_TIME (0) to mpv.
 *   - Do not emit MpvAbstractItem::ready() from synchronize(); the
 *     m_isFramebufferReady flag was removed accordingly (see mpvrenderer.h).
 *   - The return value of mpv_render_context_render() is no longer logged.
 *   - The FBO texture is created with GL_CLAMP_TO_EDGE sampling, so resizing a
 *     PiP window cannot sample the opposite edge of the video.
 *   - Formatting cleanups.
 *   - Render the video frame only when mpv produced a new one (or when a
 *     repaint was explicitly requested); unrelated scene repaints, such as UI
 *     animations, reuse the framebuffer texture instead of re-rendering the
 *     video at the compositor rate.
 *   - Measure the GPU time of the video render with timer queries and report
 *     it to MpvItem::setGpuUsage(), so plugins can show the player's own GPU
 *     usage instead of the system-wide figure.
 */

#include "mpvrenderer.h"

#include <QGuiApplication>
#include <QLoggingCategory>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QQuickWindow>
#include <QOpenGLFunctions>
#include <QtGui/qopengl.h>

#include "mpvabstractitem.h"
#include "mpvabstractitem_p.h"
#include "mpvcontroller.h"
#include "MpvItem.hpp"

Q_STATIC_LOGGING_CATEGORY(MpvQt_MpvRenderer, "MpvQt.MpvRenderer")

static void *get_proc_address_mpv(void *ctx, const char *name)
{
    Q_UNUSED(ctx)
    QOpenGLContext *glctx = QOpenGLContext::currentContext();
    if (glctx == nullptr) return nullptr;
    return reinterpret_cast<void *>(glctx->getProcAddress(QByteArray(name)));
}

void on_mpv_redraw(void *ctx)
{
    auto *r = static_cast<MpvRenderer *>(ctx);
    if (r) r->requestUpdate();
}

MpvRenderer::MpvRenderer() {}

MpvRenderer::~MpvRenderer()
{
    if (m_mpvResourceManager) m_mpvResourceManager->freeContext();
}

void MpvRenderer::synchronize(QQuickFramebufferObject *item)
{
    MpvAbstractItem *mpvAItem = static_cast<MpvAbstractItem *>(item);
    m_mpvAItem = mpvAItem;

    if (!m_mpvResourceManager) {
        m_mpvResourceManager = mpvAItem->d_ptr->m_mpvResourceManager;
    }

    if (m_frameTimer.isValid()) {
        const qint64 gap = m_frameTimer.restart();
        if (gap > 70)
            qCDebug(MpvQt_MpvRenderer) << "[perf] render gap" << gap << "ms";
    } else {
        m_frameTimer.start();
    }

    m_frameCounter = mpvAItem->frameUpdateCounter();
    MpvItem *mpvItem = qobject_cast<MpvItem *>(mpvAItem);
    if (mpvItem) {
        const bool casting = mpvItem->isCasting();
        const bool videoDisabled = mpvItem->videoDisabled();
        if (casting != m_lastCasting || videoDisabled != m_lastVideoDisabled) {
            // The framebuffer has to be cleared or filled again when video
            // output is switched on or off, even if mpv sent no new frame.
            m_lastCasting = casting;
            m_lastVideoDisabled = videoDisabled;
            m_forceRender = true;
        }
    }
}

void MpvRenderer::initGpuTimer()
{
    if (m_gpuTimerState != 0)
        return;
    m_gpuTimerState = 2;
    QOpenGLContext *ctx = QOpenGLContext::currentContext();
    if (!ctx || ctx->isOpenGLES())
        return;
    // Resolve the query functions directly: the driver may expose timer
    // queries even when Qt's version/extension check for them fails.
    m_glGenQueries = reinterpret_cast<GenQueriesFn>(ctx->getProcAddress("glGenQueries"));
    m_glBeginQuery = reinterpret_cast<BeginQueryFn>(ctx->getProcAddress("glBeginQuery"));
    m_glEndQuery = reinterpret_cast<EndQueryFn>(ctx->getProcAddress("glEndQuery"));
    m_glGetQueryObjectuiv = reinterpret_cast<GetQueryUivFn>(
        ctx->getProcAddress("glGetQueryObjectuiv"));
    m_glGetQueryObjectui64v = reinterpret_cast<GetQueryUi64vFn>(
        ctx->getProcAddress("glGetQueryObjectui64v"));
    if (!m_glGetQueryObjectui64v) {
        m_glGetQueryObjectui64v = reinterpret_cast<GetQueryUi64vFn>(
            ctx->getProcAddress("glGetQueryObjectui64vEXT"));
    }
    if (!m_glGenQueries || !m_glBeginQuery || !m_glEndQuery
            || !m_glGetQueryObjectuiv || !m_glGetQueryObjectui64v) {
        return;
    }
    m_glGenQueries(2, m_gpuQueryIds);
    if (m_gpuQueryIds[0] == 0)
        return;
    m_gpuTimerState = 1;
    qCDebug(MpvQt_MpvRenderer) << "GPU timer queries ready; context"
                               << ctx->format().majorVersion()
                               << ctx->format().minorVersion()
                               << "profile" << int(ctx->format().profile());
}

void MpvRenderer::collectGpuTimer()
{
    if (m_gpuPendingSlot < 0)
        return;
    QOpenGLContext *ctx = QOpenGLContext::currentContext();
    if (!ctx)
        return;
    GLuint available = 0;
    m_glGetQueryObjectuiv(m_gpuQueryIds[m_gpuPendingSlot],
                          GL_QUERY_RESULT_AVAILABLE, &available);
    if (!available)
        return;

    GLuint64 elapsed = 0;
    m_glGetQueryObjectui64v(m_gpuQueryIds[m_gpuPendingSlot], GL_QUERY_RESULT, &elapsed);
    m_gpuPendingSlot = -1;
    if (m_lastFrameNs <= 0)
        return;

    // Share of the frame period the video rendering occupied on the GPU.
    const double frameUsage = 100.0 * double(elapsed) / double(m_lastFrameNs);
    m_gpuUsageEma = m_gpuUsageEma < 0.0 ? frameUsage
                                        : 0.7 * m_gpuUsageEma + 0.3 * frameUsage;
    if (!m_gpuReportClock.isValid())
        m_gpuReportClock.start();
    if (m_gpuReportClock.elapsed() < 500)
        return;
    m_gpuReportClock.restart();
    const int usage = int(qBound(0.0, m_gpuUsageEma, 100.0));
    if (auto *item = qobject_cast<MpvItem *>(m_mpvAItem.data())) {
        QMetaObject::invokeMethod(item, "setGpuUsage", Qt::QueuedConnection,
                                  Q_ARG(int, usage));
    }
}

void MpvRenderer::render()
{
    if (!m_mpvAItem || !m_mpvResourceManager || !m_mpvResourceManager->mpvRenderContext) {
        return;
    }

    QOpenGLFramebufferObject *fbo = framebufferObject();
    if (!fbo) return;

    initGpuTimer();
    collectGpuTimer();

    // A repaint that is not caused by a new mpv frame (a UI animation, for
    // example) keeps the framebuffer texture as it is. Re-rendering the video
    // at the compositor rate only adds work and can drop frames.
    if (!m_forceRender && m_frameCounter == m_renderedCounter)
        return;
    m_forceRender = false;
    m_renderedCounter = m_frameCounter;

    bool measure = false;
    QOpenGLContext *glctx = QOpenGLContext::currentContext();
    if (m_gpuTimerState == 1 && m_gpuPendingSlot < 0 && glctx) {
        m_lastFrameNs = m_frameClock.isValid() ? m_frameClock.restart() : 0;
        m_gpuPendingSlot = m_gpuQuerySlot;
        m_gpuQuerySlot ^= 1;
        m_glBeginQuery(GL_TIME_ELAPSED, m_gpuQueryIds[m_gpuPendingSlot]);
        if (glctx->functions()->glGetError() != GL_NO_ERROR) {
            // The context exports the query functions but does not accept
            // GL_TIME_ELAPSED; leave the value to the plugin's own sources.
            m_gpuTimerState = 2;
            m_gpuPendingSlot = -1;
        } else {
            measure = true;
        }
    }

    QOpenGLFunctions *f = QOpenGLContext::currentContext()->functions();
    f->glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    f->glClear(GL_COLOR_BUFFER_BIT);

    MpvItem *mpvItem = qobject_cast<MpvItem*>(m_mpvAItem);
    if (mpvItem && (mpvItem->isCasting() || mpvItem->videoDisabled())) {
        return;
    }

    mpv_opengl_fbo mpfbo;
    mpfbo.fbo = static_cast<int>(fbo->handle());
    mpfbo.w = fbo->width();
    mpfbo.h = fbo->height();
    mpfbo.internal_format = 0;

    int flip_y{0};
    int block_for_target_time = 0;

    mpv_render_param params[] = {
        {MPV_RENDER_PARAM_OPENGL_FBO, &mpfbo},
        {MPV_RENDER_PARAM_FLIP_Y, &flip_y},
        {MPV_RENDER_PARAM_BLOCK_FOR_TARGET_TIME, &block_for_target_time},
        {MPV_RENDER_PARAM_INVALID, nullptr}
    };

    mpv_render_context_render(m_mpvResourceManager->mpvRenderContext, params);

    if (measure && glctx)
        m_glEndQuery(GL_TIME_ELAPSED);
}

QOpenGLFramebufferObject *MpvRenderer::createFramebufferObject(const QSize &size)
{
    // A new framebuffer needs a frame even if mpv has not produced a new one.
    m_forceRender = true;
    if (m_mpvResourceManager && !m_mpvResourceManager->mpvRenderContext) {
        m_mpvResourceManager->mpvRenderContext = createMpvRenderContext();
    }
    QOpenGLFramebufferObject *fbo = QQuickFramebufferObject::Renderer::createFramebufferObject(size);
    // Clamp edge samples to the frame; repeat sampling could show the opposite
    // edge of the video while the PiP item is being resized.
    if (fbo && fbo->texture() != 0) {
        QOpenGLFunctions *f = QOpenGLContext::currentContext()->functions();
        fbo->bind();
        f->glBindTexture(GL_TEXTURE_2D, fbo->texture());
        f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        f->glBindTexture(GL_TEXTURE_2D, 0);
        fbo->release();
    }
    return fbo;
}

mpv_render_context *MpvRenderer::createMpvRenderContext()
{
    mpv_opengl_init_params gl_init_params{get_proc_address_mpv, nullptr};
    mpv_render_param display{MPV_RENDER_PARAM_INVALID, nullptr};

    #if defined(Q_OS_UNIX) && !defined(Q_OS_DARWIN) && !defined(Q_OS_ANDROID) && !defined(Q_OS_HAIKU)
    const QString platform = QGuiApplication::platformName();

    // Luma is Wayland-only; there is deliberately no X11 display path here.
    if (platform.startsWith(QStringLiteral("wayland"))) {
        if (auto *ni = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>()) {
            display.type = MPV_RENDER_PARAM_WL_DISPLAY;
            display.data = ni->display();
            qCDebug(MpvQt_MpvRenderer) << "mpv: using Wayland display";
        }
    }
    #endif

    mpv_render_param params[]{
        {MPV_RENDER_PARAM_API_TYPE, const_cast<char *>(MPV_RENDER_API_TYPE_OPENGL)},
        {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &gl_init_params},
        display,
        {MPV_RENDER_PARAM_INVALID, nullptr}
    };

    mpv_render_context *renderCtx = nullptr;
    mpv_handle *handle = m_mpvResourceManager->mpvHandleManager->mpvHandle;
    int result = mpv_render_context_create(&renderCtx, handle, params);
    if (result < 0) {
        qCCritical(MpvQt_MpvRenderer)
        << "failed to initialize mpv GL context:" << mpv_error_string(result);
        return nullptr;
    }

    mpv_render_context_set_update_callback(renderCtx, on_mpv_redraw, this);
    return renderCtx;
}

void MpvRenderer::requestUpdate()
{
    if (m_mpvAItem) {
        QMetaObject::invokeMethod(m_mpvAItem.data(), "requestUpdateFromRenderer", Qt::QueuedConnection);
    }
}
