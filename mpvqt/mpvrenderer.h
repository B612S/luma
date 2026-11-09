/*
 * SPDX-FileCopyrightText: 2023 George Florea Bănuș <georgefb899@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL
 *
 * Modified for Luma, relative to upstream mpvqt v1.2.1:
 *   - Include <QOpenGLFramebufferObject> for the createFramebufferObject()
 *     return type.
 *   - Remove the m_isFramebufferReady flag; renderer readiness is no longer
 *     reported through MpvAbstractItem::ready() (see mpvrenderer.cpp).
 *   - Remove upstream implementation comments and redundant blank lines.
 *   - Render the video frame only when mpv produced a new one (or when a
 *     repaint was explicitly requested); unrelated scene repaints, such as UI
 *     animations, reuse the framebuffer texture instead of re-rendering the
 *     video at the compositor rate.
 *   - Measure the GPU time of the video render with timer queries and report
 *     it to MpvItem::setGpuUsage(), so plugins can show the player's own GPU
 *     usage instead of the system-wide figure.
 */

#ifndef MPVRENDERER_H
#define MPVRENDERER_H

#include <QtQuick/QQuickFramebufferObject>
#include <QOpenGLFramebufferObject>
#include <QElapsedTimer>
#include <mpv/render_gl.h>
#include "mpvabstractitem.h"

class MpvAbstractItem;

class MpvRenderer : public QQuickFramebufferObject::Renderer
{
public:
    explicit MpvRenderer();
    ~MpvRenderer();

    QOpenGLFramebufferObject *createFramebufferObject(const QSize &size) override;
    void render() override;
    void synchronize(QQuickFramebufferObject *item) override;
    void requestUpdate();

private:
    mpv_render_context *createMpvRenderContext();
    QPointer<MpvAbstractItem> m_mpvAItem{nullptr};
    std::shared_ptr<MpvResourceManager> m_mpvResourceManager;
    // The frame the framebuffer currently holds and the one the item asked
    // for. They differ only when mpv delivered a new frame (or an explicit
    // repaint was requested), so other scene repaints can reuse the texture.
    quint64 m_frameCounter = 0;
    quint64 m_renderedCounter = 0;
    bool m_forceRender = true;
    bool m_lastCasting = false;
    bool m_lastVideoDisabled = false;
    QElapsedTimer m_frameTimer;
    // GPU time of the video render (GL timer queries). 0 = not checked yet,
    // 1 = available, 2 = unsupported.
    int m_gpuTimerState = 0;
    GLuint m_gpuQueryIds[2] = {0, 0};
    int m_gpuQuerySlot = 0;
    int m_gpuPendingSlot = -1;
    QElapsedTimer m_frameClock;
    qint64 m_lastFrameNs = 0;
    double m_gpuUsageEma = -1.0;
    QElapsedTimer m_gpuReportClock;
    void initGpuTimer();
    void collectGpuTimer();
    using GenQueriesFn = void (*)(GLsizei, GLuint *);
    using BeginQueryFn = void (*)(GLenum, GLuint);
    using EndQueryFn = void (*)(GLenum);
    using GetQueryUivFn = void (*)(GLuint, GLenum, GLuint *);
    using GetQueryUi64vFn = void (*)(GLuint, GLenum, GLuint64 *);
    GenQueriesFn m_glGenQueries = nullptr;
    BeginQueryFn m_glBeginQuery = nullptr;
    EndQueryFn m_glEndQuery = nullptr;
    GetQueryUivFn m_glGetQueryObjectuiv = nullptr;
    GetQueryUi64vFn m_glGetQueryObjectui64v = nullptr;
};

#endif // MPVRENDERER_H
