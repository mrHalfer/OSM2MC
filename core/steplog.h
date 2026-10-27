#pragma once
// Debug timing of steps: if the MCX_LOG_STEPS=path_to_file environment variable is set, tmcStepLog() appends a line
// "[mcx] time step milliseconds_since_previous_mark" to the file. Used to find slow places (the GUI application has no console).
#include <QByteArray>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QString>
#include <QMutex>
#include <QMutexLocker>

inline void tmcStepLog(const char* name)
{
    static QMutex mutex;
    static QElapsedTimer timer;
    QMutexLocker lock(&mutex);
    const QByteArray target = qgetenv("MCX_LOG_STEPS");
    if (target.size() <= 1) return;
    const qint64 ms = timer.isValid() ? timer.restart() : 0;
    if (!timer.isValid()) timer.start();
    QFile f(QString::fromLocal8Bit(target));
    if (f.open(QIODevice::Append | QIODevice::Text))
        f.write(QStringLiteral("[mcx] %1 %2 %3 ms\n").arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss")), QString::fromUtf8(name)).arg(ms).toUtf8());
}
