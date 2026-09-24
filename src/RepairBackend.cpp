#include "RepairBackend.h"
#include "ProcessRunner.h"

#include <QRegularExpression>
#include <QStandardPaths>

RepairBackend::RepairBackend(QObject *parent)
    : QObject(parent)
{
}

bool RepairBackend::start(const QString &program, const QStringList &args)
{
    if (m_busy)
        return false;

    const QString executable = QStandardPaths::findExecutable(program);
    if (executable.isEmpty()) {
        m_state = QStringLiteral("error");
        m_output = tr("Comando non disponibile: %1").arg(program);
        emit stateChanged();
        return false;
    }

    auto *runner = new ProcessRunner(this);
    m_runner = runner;
    m_busy = true;
    m_state = QStringLiteral("running");
    m_output.clear();
    emit stateChanged();

    connect(runner, &ProcessRunner::finished, this,
            [this, runner](ProcessRunner::Outcome outcome, int,
                           const QByteArray &stdoutData, const QByteArray &stderrData,
                           const QString &errorString) {
        if (runner != m_runner)
            return;

        m_runner = nullptr;
        runner->deleteLater();
        m_busy = false;

        QByteArray combined = stdoutData;
        if (!stderrData.isEmpty()) {
            if (!combined.isEmpty() && !combined.endsWith('\n'))
                combined.append('\n');
            combined.append(stderrData);
        }
        m_output = QString::fromUtf8(combined).trimmed();

        if (outcome == ProcessRunner::Success) {
            m_state = QStringLiteral("success");
        } else if (outcome == ProcessRunner::Cancelled) {
            m_state = QStringLiteral("cancelled");
        } else if (outcome == ProcessRunner::TimedOut) {
            m_state = QStringLiteral("timeout");
        } else {
            m_state = QStringLiteral("error");
            if (m_output.isEmpty())
                m_output = errorString;
        }
        emit stateChanged();
    });

    ProcessRunner::Options options;
    options.program = executable;
    options.arguments = args;
    options.timeoutMs = 60 * 1000;
    options.processGroup = true;
    options.mergedChannels = true;
    if (!runner->start(options)) {
        m_runner = nullptr;
        runner->deleteLater();
        m_busy = false;
        m_state = QStringLiteral("error");
        emit stateChanged();
        return false;
    }
    return true;
}

bool RepairBackend::restartAudio()
{
    return start(QStringLiteral("systemctl"),
                 {QStringLiteral("--user"), QStringLiteral("restart"),
                  QStringLiteral("pipewire.service"),
                  QStringLiteral("pipewire-pulse.service"),
                  QStringLiteral("wireplumber.service")});
}

bool RepairBackend::flushDns()
{
    return start(QStringLiteral("resolvectl"), {QStringLiteral("flush-caches")});
}

bool RepairBackend::reconnectNetwork(const QString &interfaceName)
{
    static const QRegularExpression safeInterface(QStringLiteral("^[A-Za-z0-9_.:-]{1,32}$"));
    if (!safeInterface.match(interfaceName).hasMatch())
        return false;
    return start(QStringLiteral("nmcli"),
                 {QStringLiteral("device"), QStringLiteral("reapply"), interfaceName});
}

bool RepairBackend::applyDnsPreset(const QString &interfaceName, const QString &preset)
{
    static const QRegularExpression safeInterface(QStringLiteral("^[A-Za-z0-9_.:-]{1,32}$"));
    if (!safeInterface.match(interfaceName).hasMatch())
        return false;

    QString dns;
    if (preset == QStringLiteral("cloudflare"))
        dns = QStringLiteral("1.1.1.1,1.0.0.1");
    else if (preset == QStringLiteral("quad9"))
        dns = QStringLiteral("9.9.9.9,149.112.112.112");
    else if (preset == QStringLiteral("google"))
        dns = QStringLiteral("8.8.8.8,8.8.4.4");
    else if (preset != QStringLiteral("automatic"))
        return false;

    return start(QStringLiteral("nmcli"),
                 {QStringLiteral("device"), QStringLiteral("modify"), interfaceName,
                  QStringLiteral("ipv4.ignore-auto-dns"),
                  preset == QStringLiteral("automatic") ? QStringLiteral("no")
                                                        : QStringLiteral("yes"),
                  QStringLiteral("ipv4.dns"), dns});
}

bool RepairBackend::cancel()
{
    return m_runner && m_runner->cancel();
}
