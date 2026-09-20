#include "RkBackend.h"

#include "PolkitHelper.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QTimer>

namespace {
constexpr int kStatusTimeoutMs = 30 * 1000;
}


RkBackend::RkBackend(PolkitHelper *polkit, QObject *parent)
    : QObject(parent)
    , m_polkit(polkit)
{
    if (m_polkit) {
        connect(m_polkit, &PolkitHelper::runningChanged, this, &RkBackend::stateChanged);
        connect(m_polkit, &PolkitHelper::line, this, [this](const QString &line) {
            if (!m_operationOwned)
                return;
            m_operationLines.append(line);
            while (m_operationLines.size() > 12)
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

bool RkBackend::canChangePackages() const
{
    return m_statusValid
        && m_overlayState == QStringLiteral("ready")
        && !m_pendingRecovery
        && !m_needsSync
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
        const QByteArray outputData = process->readAllStandardOutput();
        const QString output = QString::fromUtf8(outputData).trimmed();
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

        parseStatus(outputData);
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

    rawProcess->start(QStringLiteral("/usr/bin/rk"),
                      {QStringLiteral("status"), QStringLiteral("--json")});
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

bool RkBackend::addPackage(const QString &packageName)
{
    const QString package = packageName.trimmed();
    if (!canChangePackages() || !validPackageName(package))
        return false;
    startPrivileged({QStringLiteral("add"), package});
    return true;
}

bool RkBackend::removePackage(const QString &packageName)
{
    const QString package = packageName.trimmed();
    if (!canChangePackages() || !validPackageName(package))
        return false;
    startPrivileged({QStringLiteral("rm"), package});
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

void RkBackend::parseStatus(const QByteArray &data)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        finishStatusError(tr("rk status --json non valido: %1").arg(parseError.errorString()));
        return;
    }

    const QJsonObject object = document.object();
    if (object.value(QStringLiteral("schema")).toInt(-1) != 1) {
        finishStatusError(tr("Versione del contratto rk status non supportata."));
        return;
    }

    const QString overlay = object.value(QStringLiteral("overlay")).toString();
    if (overlay != QStringLiteral("ready") && overlay != QStringLiteral("degraded")) {
        finishStatusError(tr("Stato overlay rk non riconosciuto."));
        return;
    }
    if (!object.value(QStringLiteral("pending_recovery")).isBool()
        || !object.value(QStringLiteral("needs_sync")).isBool()
        || !object.value(QStringLiteral("requests")).isArray()) {
        finishStatusError(tr("rk status --json è incompleto."));
        return;
    }

    QStringList requests;
    for (const QJsonValue &value : object.value(QStringLiteral("requests")).toArray()) {
        if (!value.isString() || !validPackageName(value.toString())) {
            finishStatusError(tr("rk status contiene una richiesta pacchetto non valida."));
            return;
        }
        requests.append(value.toString());
    }

    m_statusText = QString::fromUtf8(document.toJson(QJsonDocument::Indented)).trimmed();
    m_errorText.clear();
    m_overlayState = overlay;
    m_pendingRecovery = object.value(QStringLiteral("pending_recovery")).toBool();
    m_needsSync = object.value(QStringLiteral("needs_sync")).toBool();
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
    m_operationLines.clear();
    emit operationStateChanged();
    m_polkit->execute(QStringLiteral("/usr/bin/rk"), args);
}
