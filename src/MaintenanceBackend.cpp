#include "MaintenanceBackend.h"

#include <QFileInfo>
#include <QTimer>

namespace {
constexpr int kTrashTimeoutMs = 10 * 60 * 1000;
constexpr qsizetype kMaxOutput = 128 * 1024;
}

MaintenanceBackend::MaintenanceBackend(QObject *parent)
    : QObject(parent)
{
}

bool MaintenanceBackend::available() const
{
    const QFileInfo helper(QStringLiteral("/usr/libexec/kriscc/maintenance"));
    return !m_running && helper.exists() && helper.isExecutable();
}

bool MaintenanceBackend::cleanTrash(const QString &scope)
{
    if (!available())
        return false;

    QString argument;
    if (scope == QStringLiteral("home"))
        argument = QStringLiteral("trash-home");
    else if (scope == QStringLiteral("system"))
        argument = QStringLiteral("trash-system");
    else if (scope == QStringLiteral("all"))
        argument = QStringLiteral("trash-all");
    else
        return false;

    auto *process = new QProcess(this);
    const QPointer<QProcess> guarded(process);
    m_process = process;
    m_scope = scope;
    m_output.clear();
    m_resultState = QStringLiteral("running");
    m_running = true;
    m_cancelRequested = false;
    m_timedOut = false;
    emit stateChanged();

    process->setProcessChannelMode(QProcess::MergedChannels);
    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this, guarded](int exitCode, QProcess::ExitStatus status) {
        if (!guarded || guarded != m_process)
            return;
        QString output = QString::fromUtf8(guarded->readAllStandardOutput()).trimmed();
        if (output.size() > kMaxOutput)
            output = tr("[output precedente omesso]\n") + output.right(kMaxOutput);
        guarded->deleteLater();
        m_process = nullptr;

        if (m_cancelRequested) {
            complete(QStringLiteral("cancelled"),
                     tr("Pulizia cestini annullata."), false);
        } else if (m_timedOut) {
            complete(QStringLiteral("timeout"),
                     tr("Tempo massimo superato durante la pulizia cestini."), false);
        } else {
            const bool success = status == QProcess::NormalExit && exitCode == 0;
            complete(success ? QStringLiteral("success") : QStringLiteral("error"),
                     output.isEmpty() && !success
                         ? tr("Pulizia cestini terminata con codice %1.").arg(exitCode)
                         : output,
                     success);
        }
    });

    connect(process, &QProcess::errorOccurred, this,
            [this, guarded](QProcess::ProcessError error) {
        if (!guarded || guarded != m_process || error != QProcess::FailedToStart)
            return;
        const QString message = guarded->errorString();
        guarded->deleteLater();
        m_process = nullptr;
        complete(QStringLiteral("error"),
                 tr("Impossibile avviare la pulizia cestini: %1").arg(message), false);
    });

    process->start(QStringLiteral("/usr/libexec/kriscc/maintenance"), {argument});
    QTimer::singleShot(kTrashTimeoutMs, process, [this, guarded] {
        if (!guarded || guarded != m_process || guarded->state() == QProcess::NotRunning)
            return;
        m_timedOut = true;
        guarded->terminate();
        QTimer::singleShot(3000, guarded, [guarded] {
            if (guarded && guarded->state() != QProcess::NotRunning)
                guarded->kill();
        });
    });
    return true;
}

void MaintenanceBackend::cancel()
{
    if (!m_process || !m_running)
        return;
    m_cancelRequested = true;
    m_process->terminate();
    const QPointer<QProcess> guarded = m_process;
    QTimer::singleShot(3000, guarded, [guarded] {
        if (guarded && guarded->state() != QProcess::NotRunning)
            guarded->kill();
    });
}

void MaintenanceBackend::complete(const QString &state, const QString &output, bool success)
{
    m_running = false;
    m_cancelRequested = false;
    m_timedOut = false;
    m_resultState = state;
    m_output = output;
    emit stateChanged();
    emit finished(success, output);
}
