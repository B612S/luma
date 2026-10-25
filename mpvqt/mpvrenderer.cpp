/*
 * SPDX-FileCopyrightText: 2023 George Florea Bănuș <georgefb899@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL
 */

#include "mpvrenderer.h"

#include <QGuiApplication>
#include <QLoggingCategory>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QQuickWindow>
#include <QOpenGLFunctions>

// ВАЖНО: <QtGui/qnativeinterface.h> не существует в стандартной установке Qt6.
// QNativeInterface::QWaylandApplication / QX11Application объявлены в <QGuiApplication>
// (через макросы Q_DECLARE_NATIVE_INTERFACE), поэтому отдельный include не нужен.

#include "mpvabstractitem.h"
#include "mpvabstractitem_p.h"
#include "mpvcontroller.h"

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
}

void MpvRenderer::render()
{
    if (!m_mpvAItem || !m_mpvResourceManager || !m_mpvResourceManager->mpvRenderContext) {
        return;
    }

    QOpenGLFramebufferObject *fbo = framebufferObject();
    if (!fbo) return;

    QOpenGLFunctions *f = QOpenGLContext::currentContext()->functions();
    f->glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    f->glClear(GL_COLOR_BUFFER_BIT);

    // Если видео нет — mpv сам не нарисует кадр, FBO останется чёрным.
    // Блокирующий getProperty("vid") здесь не нужен (и опасен — render thread).

    mpv_opengl_fbo mpfbo;
    mpfbo.fbo = static_cast<int>(fbo->handle());
    mpfbo.w = fbo->width();
    mpfbo.h = fbo->height();
    mpfbo.internal_format = 0;

    // Qt FBO уже в OpenGL-ориентации
    int flip_y{0};
    int block_for_target_time = 0;

    mpv_render_param params[] = {
        {MPV_RENDER_PARAM_OPENGL_FBO, &mpfbo},
        {MPV_RENDER_PARAM_FLIP_Y, &flip_y},
        {MPV_RENDER_PARAM_BLOCK_FOR_TARGET_TIME, &block_for_target_time},
        {MPV_RENDER_PARAM_INVALID, nullptr}
    };

    mpv_render_context_render(m_mpvResourceManager->mpvRenderContext, params);
}

QOpenGLFramebufferObject *MpvRenderer::createFramebufferObject(const QSize &size)
{
    if (m_mpvResourceManager && !m_mpvResourceManager->mpvRenderContext) {
        m_mpvResourceManager->mpvRenderContext = createMpvRenderContext();
    }
    return QQuickFramebufferObject::Renderer::createFramebufferObject(size);
}

mpv_render_context *MpvRenderer::createMpvRenderContext()
{
    mpv_opengl_init_params gl_init_params{get_proc_address_mpv, nullptr};
    mpv_render_param display{MPV_RENDER_PARAM_INVALID, nullptr};

    #if defined(Q_OS_UNIX) && !defined(Q_OS_DARWIN) && !defined(Q_OS_ANDROID) && !defined(Q_OS_HAIKU)
    const QString platform = QGuiApplication::platformName();

    // startsWith("wayland") — потому что platformName() может быть "wayland",
    // "wayland-egl", "wayland-brcm" и т.п.
    if (platform.startsWith(QStringLiteral("wayland"))) {
        // Нативный Wayland: передаём wl_display* в libmpv.
        // QNativeInterface::QWaylandApplication доступен с Qt 6.5.
        if (auto *ni = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>()) {
            display.type = MPV_RENDER_PARAM_WL_DISPLAY;
            display.data = ni->display();
            qCDebug(MpvQt_MpvRenderer) << "mpv: using Wayland display";
        } else {
            qCWarning(MpvQt_MpvRenderer)
            << "mpv: QWaylandApplication native interface is not available";
        }
    } else if (platform == QStringLiteral("xcb")) {
        // X11 (в том числе XWayland): передаём Display* в libmpv.
        if (auto *ni = qGuiApp->nativeInterface<QNativeInterface::QX11Application>()) {
            display.type = MPV_RENDER_PARAM_X11_DISPLAY;
            display.data = ni->display();
            qCDebug(MpvQt_MpvRenderer) << "mpv: using X11 display";
        } else {
            qCWarning(MpvQt_MpvRenderer)
            << "mpv: QX11Application native interface is not available";
        }
    } else {
        qCWarning(MpvQt_MpvRenderer)
        << "mpv: unknown platform" << platform
        << "— display param left as INVALID";
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
