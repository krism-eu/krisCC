#include "MaintenanceBackend.h"

#include "ProcessRunner.h"

#include <QFileInfo>

namespace {
constexpr int kTrashTimeoutMs = 10 * 60 * 1000;
constexpr qsizetype kMaxOutput = 128 * 1024;
}

MaintenanceBackend::MaintenanceBackend(QObject *parent) : QObject(parent) {}

bool MaintenanceBackend::available() const
{
    const QFileInfo helper(QStringLiteral("/usr/libexec/kriscc/maintenance"));
    return !m_running && helper.exists() && helper.isExecutable();
}

bool MaintenanceBackend::cleanTrash(const QString &scope)
{
    if (!available()) return false;

    QString argument;
    if (scope == QStringLiteral("home")) argument = QStringLiteral("trash-home");
    else if (scope == QStringLiteral("system")) argument = QStringLiteral("trash-system");
    else if (scope == QStringLiteral("all")) argument = QStringLiteral("trash-all");
    else return false;

    auto *runner = new ProcessRunner(this);
    m_runner = runner;
    m_scope = scope;
    m_output.clear();
    m_resultState = QStringLiteral("running");
    m_running = true;
    emit stateChanged();

    connect(runner, &ProcessRunner::finished, this,
            [this, runner](ProcessRunner::Outcome outcome, int exitCode,
                           const QByteArray &out, const QByteArray &err, const QString &error) {
        if (runner != m_runner) { runner->deleteLater(); return; }
        m_runner = nullptr;
        runner->deleteLater();
        QString output = QString::fromUtf8(err.isEmpty() ? out : out + err).trimmed();
        switch (outcome) {
        case ProcessRunner::Success:
            complete(QStringLiteral("success"), output, true);
            break;
        case ProcessRunner::Cancelled:
            complete(QStringLiteral("cancelled"), tr("Pulizia cestini annullata."), false);
            break;
        case ProcessRunner::TimedOut:
            complete(QStringLiteral("timeout"), tr("Tempo massimo superato durante la pulizia cestini."), false);
            break;
        case ProcessRunner::FailedToStart:
            complete(QStringLiteral("error"), tr("Impossibile avviare la pulizia cestini: %1").arg(error), false);
            break;
        case ProcessRunner::ExitError:
            complete(QStringLiteral("error"),
                     output.isEmpty() ? tr("Pulizia cestini terminata con codice %1.").arg(exitCode) : output,
                     false);
            break;
        }
    });

    ProcessRunner::Options options;
    options.program = QStringLiteral("/usr/libexec/kriscc/maintenance");
    options.arguments = {argument};
    options.timeoutMs = kTrashTimeoutMs;
    options.maxOutputBytes = kMaxOutput;
    options.mergedChannels = true;
    if (!runner->start(options)) {
        m_runner = nullptr;
        runner->deleteLater();
        complete(QStringLiteral("error"), tr("Impossibile inizializzare la pulizia cestini."), false);
        return false;
    }
    return true;
}

void MaintenanceBackend::cancel()
{
    if (m_runner && m_running)
        m_runner->cancel();
}

void MaintenanceBackend::complete(const QString &state, const QString &output, bool success)
{
    m_running = false;
    m_resultState = state;
    m_output = output;
    emit stateChanged();
    emit finished(success, output);
}
