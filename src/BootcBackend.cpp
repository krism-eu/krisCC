#include "BootcBackend.h"

#include "PolkitHelper.h"
#include "ContractParsers.h"

#include <QFile>
#include <QFileInfo>
#include <QTimer>

#include <unistd.h>

namespace {
void startBootcStatus(QProcess *process, const QString &format)
{
    QStringList bootcArgs = {
        QStringLiteral("status"), QStringLiteral("--format"), format
    };
    if (format == QStringLiteral("json"))
        bootcArgs.append(QStringLiteral("--format-version=1"));

    if (::geteuid() == 0) {
        process->start(QStringLiteral("/usr/bin/bootc"), bootcArgs);
        return;
    }

    process->start(QStringLiteral("/usr/bin/pkexec"),
                   {QStringLiteral("/usr/libexec/kriscc/bootc-status"), format});
}
}

BootcBackend::BootcBackend(PolkitHelper *polkit, QObject *parent)
    : QObject(parent)
    , m_polkit(polkit)
{
    if (m_polkit) {
        connect(m_polkit, &PolkitHelper::runningChanged, this, &BootcBackend::operationStateChanged);
        connect(m_polkit, &PolkitHelper::line, this, [this](const QString &line) {
            if (!m_operationOwned)
                return;
            m_operationLines.append(line);
            while (m_operationLines.size() > 14)
                m_operationLines.removeFirst();
            emit operationStateChanged();
        });
        connect(m_polkit, &PolkitHelper::finished, this,
                [this](bool success, const QString &output) {
            if (!m_operationOwned)
                return;
            m_operationOwned = false;
            m_operationRunning = false;
            m_operationState = success ? QStringLiteral("success") : QStringLiteral("error");
            if (m_operationLines.isEmpty() && !output.trimmed().isEmpty())
                m_operationLines.append(output.trimmed());
            emit operationStateChanged();
            emit operationFinished(success, output);
            refreshStatus();
            refreshPackages();
        });
    }

    loadPackages();
}

bool BootcBackend::canOperate() const
{
    return bootcAvailable() && !m_busy && m_polkit && !m_polkit->running() && !m_operationRunning;
}

bool BootcBackend::checkUpgrade()
{
    return startPrivileged({QStringLiteral("bootc-check")});
}

bool BootcBackend::downloadUpgrade()
{
    return startPrivileged({QStringLiteral("bootc-download")});
}

bool BootcBackend::prepareUpgrade()
{
    return startPrivileged({QStringLiteral("bootc-prepare")});
}

bool BootcBackend::applyDownloaded()
{
    return startPrivileged({QStringLiteral("bootc-apply-downloaded")});
}

bool BootcBackend::startPrivileged(const QStringList &args)
{
    if (!canOperate())
        return false;
    m_operationOwned = true;
    m_operationRunning = true;
    m_operationState = QStringLiteral("running");
    m_operationLines.clear();
    emit operationStateChanged();
    m_polkit->execute(QStringLiteral("/usr/libexec/kriscc/admin"), args);
    return true;
}

bool BootcBackend::bootcAvailable() const
{
    const QFileInfo info(QStringLiteral("/usr/bin/bootc"));
    return info.exists() && info.isExecutable();
}

void BootcBackend::refreshStatus()
{
    if (m_busy)
        return;

    if (!bootcAvailable()) {
        m_statusText = tr("bootc non disponibile su questo sistema.");
        m_errorText.clear();
        m_deployments.clear();
        emit statusChanged();
        return;
    }

    setBusy(true);
    m_errorText.clear();
    m_deployments.clear();

    auto *rawProcess = new QProcess(this);
    const QPointer<QProcess> process(rawProcess);
    m_process = rawProcess;
    rawProcess->setProcessChannelMode(QProcess::SeparateChannels);

    connect(rawProcess, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this, process](int exitCode, QProcess::ExitStatus status) {
        if (!process || process != m_process)
            return;

        const bool timedOut = process->property("krisccTimedOut").toBool();
        const QByteArray stdoutData = process->readAllStandardOutput();
        const QString stderrText = QString::fromUtf8(process->readAllStandardError()).trimmed();
        m_process = nullptr;
        process->deleteLater();

        if (timedOut) {
            m_statusText.clear();
            m_errorText = tr("Tempo massimo superato durante bootc status.");
            setBusy(false);
            emit statusChanged();
            return;
        }

        if (status == QProcess::NormalExit && exitCode == 0) {
            parseJsonStatus(stdoutData);
            setBusy(false);
            emit statusChanged();
            return;
        }

        startHumanStatus(stderrText.isEmpty()
                             ? tr("bootc status --format json e' terminato con codice %1.").arg(exitCode)
                             : stderrText);
    });

    connect(rawProcess, &QProcess::errorOccurred, this,
            [this, process](QProcess::ProcessError error) {
        if (!process || process != m_process || error != QProcess::FailedToStart)
            return;
        const QString reason = process->errorString();
        m_process = nullptr;
        process->deleteLater();
        startHumanStatus(tr("Impossibile avviare bootc JSON: %1").arg(reason));
    });

    startBootcStatus(rawProcess, QStringLiteral("json"));
    QTimer::singleShot(30 * 1000, rawProcess, [this, process] {
        if (!process || process != m_process || process->state() == QProcess::NotRunning)
            return;
        process->setProperty("krisccTimedOut", true);
        process->terminate();
        QTimer::singleShot(2000, process, [process] {
            if (process && process->state() != QProcess::NotRunning)
                process->kill();
        });
    });
}

void BootcBackend::startHumanStatus(const QString &previousError)
{
    auto *rawProcess = new QProcess(this);
    const QPointer<QProcess> process(rawProcess);
    m_process = rawProcess;
    rawProcess->setProcessChannelMode(QProcess::MergedChannels);

    connect(rawProcess, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this, process, previousError](int exitCode, QProcess::ExitStatus status) {
        if (!process || process != m_process)
            return;
        const bool timedOut = process->property("krisccTimedOut").toBool();
        const QString output = QString::fromUtf8(process->readAllStandardOutput()).trimmed();
        const bool ok = status == QProcess::NormalExit && exitCode == 0;
        m_process = nullptr;
        process->deleteLater();
        m_statusText = output.isEmpty() ? tr("Nessun output da bootc status.") : output;
        m_errorText = timedOut ? tr("Tempo massimo superato durante bootc status. %1").arg(previousError)
                               : (ok ? previousError
                                     : tr("bootc status e' terminato con codice %1. %2").arg(exitCode).arg(previousError));
        setBusy(false);
        emit statusChanged();
    });

    connect(rawProcess, &QProcess::errorOccurred, this,
            [this, process, previousError](QProcess::ProcessError error) {
        if (!process || process != m_process || error != QProcess::FailedToStart)
            return;
        m_errorText = tr("Impossibile avviare bootc: %1. %2")
                          .arg(process->errorString(), previousError);
        m_statusText.clear();
        m_process = nullptr;
        process->deleteLater();
        setBusy(false);
        emit statusChanged();
    });

    startBootcStatus(rawProcess, QStringLiteral("humanreadable"));
    QTimer::singleShot(30 * 1000, rawProcess, [this, process] {
        if (!process || process != m_process || process->state() == QProcess::NotRunning)
            return;
        process->setProperty("krisccTimedOut", true);
        process->terminate();
        QTimer::singleShot(2000, process, [process] {
            if (process && process->state() != QProcess::NotRunning)
                process->kill();
        });
    });
}

void BootcBackend::parseJsonStatus(const QByteArray &data)
{
    const auto parsed = ContractParsers::parseBootcStatus(data);
    if (!parsed.ok()) {
        m_statusText = QString::fromUtf8(data).trimmed();
        using Error = ContractParsers::Error;
        if (parsed.error == Error::UnsupportedContract)
            m_errorText = tr("Versione del contratto bootc status non supportata.");
        else if (parsed.error == Error::InvalidShape)
            m_errorText = tr("Output bootc status incompleto.");
        else
            m_errorText = tr("Output JSON di bootc non valido.");
        m_deployments.clear();
        return;
    }

    m_statusText = parsed.formatted;
    m_errorText.clear();
    m_deployments = parsed.deployments;
}

void BootcBackend::refreshPackages()
{
    loadPackages();
    emit packagesChanged();
}

void BootcBackend::setBusy(bool busy)
{
    if (m_busy == busy)
        return;
    m_busy = busy;
    emit busyChanged();
    emit operationStateChanged();
}

void BootcBackend::loadPackages()
{
    QFile file(QStringLiteral("/var/lib/krisos/packages.list"));
    QStringList packages;
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        while (!file.atEnd()) {
            const QString line = QString::fromUtf8(file.readLine()).trimmed();
            if (!line.isEmpty() && !line.startsWith('#'))
                packages.append(line);
        }
    }

    m_persistentPackageCount = packages.size();
    if (packages.isEmpty()) {
        m_persistentPackages = tr("nessuno");
    } else if (packages.size() <= 12) {
        m_persistentPackages = packages.join(QStringLiteral(", "));
    } else {
        m_persistentPackages = packages.mid(0, 12).join(QStringLiteral(", "))
                             + tr(" … (+%1)").arg(packages.size() - 12);
    }
}
