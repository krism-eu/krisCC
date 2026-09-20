#include "RkBackend.h"

#include "PolkitHelper.h"

#include <QFileInfo>
#include <QRegularExpression>
#include <QTimer>

namespace {
constexpr int kStatusTimeoutMs = 30 * 1000;

bool parseBoolean(const QString &value, bool *result)
{
    if (value == QStringLiteral("True")) {
        *result = true;
        return true;
    }
    if (value == QStringLiteral("False")) {
        *result = false;
        return true;
    }
    return false;
}
}

RkBackend::RkBackend(PolkitHelper *polkit, QObject *parent)
    : QObject(parent)
    , m_polkit(polkit)
{
    if (m_polkit) {
        connect(m_polkit, &PolkitHelper::runningChanged, this, &RkBackend::stateChanged);
        connect(m_polkit, &PolkitHelper::finished, this,
                [this](bool success, const QString &output) {
            if (!m_operationOwned)
                return;

            m_operationOwned = false;
            m_operationRunning = false;
            m_operationState = success ? QStringLiteral("success") : QStringLiteral("error");
            m_operationOutput = output;
            emit operationStateChanged();
            emit operationFinished(success, output);
            refreshStatus();
        });
    }

    QTimer::singleShot(0, this, &RkBackend::refreshStatus);
}

bool RkBackend::canSync() const
{
    return m_statusValid
        && m_overlayState == QStringLiteral("ready")
        && m_needsSync
        && !m_pendingRecovery
        && !m_busy
        && m_polkit
        && !m_polkit->running();
}

bool RkBackend::canForget() const
{
    return canSync();
}

void RkBackend::refreshStatus()
{
    if (m_busy)
        return;

    const QFileInfo rk(QStringLiteral("/usr/bin/rk"));
    if (!rk.exists() || !rk.isExecutable()) {
        finishStatusError(tr("rk non è disponibile su questo sistema."));
        return;
    }

    m_busy = true;
    emit stateChanged();

    auto *rawProcess = new QProcess(this);
    const QPointer<QProcess> process(rawProcess);
    m_process = rawProcess;
    rawProcess->setProcessChannelMode(QProcess::MergedChannels);

    connect(rawProcess, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this, process](int exitCode, QProcess::ExitStatus status) {
        if (!process || process != m_process)
            return;

        const bool timedOut = process->property("krisccTimedOut").toBool();
        const QString output = QString::fromUtf8(process->readAllStandardOutput()).trimmed();
        m_process = nullptr;
        process->deleteLater();
        m_busy = false;

        if (timedOut) {
            finishStatusError(tr("Tempo massimo superato durante rk status."));
            return;
        }
        if (status != QProcess::NormalExit || exitCode != 0) {
            finishStatusError(output.isEmpty()
                                  ? tr("rk status è terminato con codice %1.").arg(exitCode)
                                  : output);
            return;
        }

        parseStatus(output);
    });

    connect(rawProcess, &QProcess::errorOccurred, this,
            [this, process](QProcess::ProcessError error) {
        if (!process || process != m_process || error != QProcess::FailedToStart)
            return;
        const QString reason = process->errorString();
        m_process = nullptr;
        process->deleteLater();
        m_busy = false;
        finishStatusError(tr("Impossibile avviare rk status: %1").arg(reason));
    });

    rawProcess->start(QStringLiteral("/usr/bin/rk"), {QStringLiteral("status")});
    QTimer::singleShot(kStatusTimeoutMs, rawProcess, [process] {
        if (!process || process->state() == QProcess::NotRunning)
            return;
        process->setProperty("krisccTimedOut", true);
        process->terminate();
        QTimer::singleShot(2000, process, [process] {
            if (process && process->state() != QProcess::NotRunning)
                process->kill();
        });
    });
}

bool RkBackend::sync()
{
    if (!canSync())
        return false;
    startPrivileged({QStringLiteral("sync")});
    return true;
}

bool RkBackend::forget(const QString &packageName)
{
    const QString package = packageName.trimmed();
    if (!canForget() || !validPackageName(package))
        return false;
    startPrivileged({QStringLiteral("forget"), package});
    return true;
}

void RkBackend::parseStatus(const QString &text)
{
    bool overlaySeen = false;
    bool pendingSeen = false;
    bool needsSeen = false;
    bool pending = false;
    bool needs = false;
    bool requestsStarted = false;
    QString overlay;
    QStringList requests;

    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString &rawLine : lines) {
        const QString line = rawLine.trimmed();
        if (line.startsWith(QStringLiteral("Overlay:"))) {
            if (overlaySeen) {
                finishStatusError(tr("rk status contiene più stati Overlay."));
                return;
            }
            overlaySeen = true;
            overlay = line.mid(QStringLiteral("Overlay:").size()).trimmed();
            if (overlay != QStringLiteral("ready") && overlay != QStringLiteral("degraded")) {
                finishStatusError(tr("Stato Overlay non riconosciuto: %1").arg(overlay));
                return;
            }
            continue;
        }

        if (line.startsWith(QStringLiteral("Pending recovery:"))) {
            if (pendingSeen
                || !parseBoolean(line.mid(QStringLiteral("Pending recovery:").size()).trimmed(), &pending)) {
                finishStatusError(tr("Valore Pending recovery non valido."));
                return;
            }
            pendingSeen = true;
            continue;
        }

        if (line.startsWith(QStringLiteral("Needs sync:"))) {
            if (needsSeen
                || !parseBoolean(line.mid(QStringLiteral("Needs sync:").size()).trimmed(), &needs)) {
                finishStatusError(tr("Valore Needs sync non valido."));
                return;
            }
            needsSeen = true;
            requestsStarted = true;
            continue;
        }

        if (requestsStarted && !line.isEmpty())
            requests.append(line);
    }

    if (!overlaySeen || !pendingSeen || !needsSeen) {
        finishStatusError(tr("rk status non contiene tutti i marker richiesti."));
        return;
    }

    m_statusText = text;
    m_errorText.clear();
    m_overlayState = overlay;
    m_pendingRecovery = pending;
    m_needsSync = needs;
    m_requests = requests;
    m_statusValid = true;
    emit stateChanged();
}

void RkBackend::finishStatusError(const QString &message)
{
    m_busy = false;
    m_statusValid = false;
    m_overlayState = QStringLiteral("unknown");
    m_pendingRecovery = false;
    m_needsSync = false;
    m_requests.clear();
    m_errorText = message;
    emit stateChanged();
}

bool RkBackend::validPackageName(const QString &packageName) const
{
    static const QRegularExpression pattern(
        QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._+:-]{0,127}$"));
    return pattern.match(packageName).hasMatch();
}

void RkBackend::startPrivileged(const QStringList &args)
{
    if (!m_polkit)
        return;

    m_operationOwned = true;
    m_operationRunning = true;
    m_operationState = QStringLiteral("running");
    m_operationOutput.clear();
    emit operationStateChanged();
    m_polkit->execute(QStringLiteral("/usr/bin/rk"), args);
}
