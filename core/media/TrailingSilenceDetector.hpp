// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 B612S <sem.alimov@inbox.ru>

#pragma once
#include <QObject>
#include <QString>

// Decodes the tail of an audio file in a worker thread and reports the
// position after which only silence remains. Used by the music crossfade.
class TrailingSilenceDetector : public QObject {
    Q_OBJECT

public:
    explicit TrailingSilenceDetector(QObject *parent = nullptr);

    void request(const QString &path, quint64 generation);

signals:
    void detected(const QString &path, quint64 generation, double effectiveEnd);
};
