#include "ServiceManagerBackend.h"

#include "OperationLog.h"
#include "ProcessRunner.h"
#include "ServiceJson.h"

#include <QHash>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QVariantMap>

#include <algorithm>

namespace {
constexpr qsizetype kMaxOutput = 1024 * 1024;

QString scopeName(bool userScope)
{
    return userScope ? QStringLiteral("user") : QStringLiteral("system");
}

}

ServiceManagerBackend::ServiceManagerBackend(QObject *parent)
    : QObject(parent)
{
}

ServiceManagerBackend::~ServiceManagerBackend() = default;

bool ServiceManagerBackend::validUnit(const QString &unit) const
{
    static const QRegularExpression pattern(
        QStringLiteral("^[A-Za-z0-9_.@:-]{1,120}\\.service$"));
    return pattern.match(unit).hasMatch();
}

bool ServiceManagerBackend::validJournalUnit(const QString &unit) const
{
    static const QRegularExpression pattern(
        QStringLiteral("^[A-Za-z0-9_.@:-]{1,120}\\.(?:service|socket|timer)$"));
    return pattern.match(unit).hasMatch();
}

bool ServiceManagerBackend::startProcess(const QStringList &arguments, Task task, int timeoutMs)
{
    if (m_busy)
        return false;

    const QString executable = QStandardPaths::findExecutable(QStringLiteral("systemctl"));
    if (executable.isEmpty()) {
        finish(QStringLiteral("error"), tr("systemctl non è disponibile."));
        return false;
    }

    auto *runner = new ProcessRunner(this);
    m_runner = runner;
    m_busy = true;
    m_state = QStringLiteral("running");
    m_message.clear();
    emit stateChanged();

    connect(runner, &ProcessRunner::finished, this,
            [this, runner, task](ProcessRunner::Outcome outcome, int exitCode,
                                 const QByteArray &standardOutput,
                                 const QByteArray &standardError,
                                 const QString &errorString) {
        if (runner != m_runner)
            return;
        const bool truncated = runner->outputTruncated();
        m_runner = nullptr;
        runner->deleteLater();
        m_busy = false;
        handleFinished(task, exitCode, int(outcome),
                       standardOutput, standardError, errorString, truncated);
    });

    ProcessRunner::Options options;
    options.program = executable;
    options.arguments = arguments;
    options.timeoutMs = timeoutMs;
    options.maxOutputBytes = kMaxOutput;
    options.mergedChannels = false;
    options.processGroup = true;
    if (!runner->start(options)) {
        m_runner = nullptr;
        runner->deleteLater();
        m_busy = false;
        finish(QStringLiteral("error"), tr("Impossibile avviare systemctl."));
        return false;
    }
    return true;
}

void ServiceManagerBackend::finish(const QString &state, const QString &message)
{
    m_busy = false;
    m_state = state;
    m_message = message;
    emit stateChanged();
}

bool ServiceManagerBackend::refreshServices(bool userScope)
{
    if (m_busy)
        return false;
    m_userScope = userScope;
    m_pendingServices.clear();

    QStringList args;
    if (userScope)
        args << QStringLiteral("--user");
    args << QStringLiteral("list-units")
         << QStringLiteral("--type=service")
         << QStringLiteral("--all")
         << QStringLiteral("--output=json")
         << QStringLiteral("--no-pager");
    return startProcess(args, Task::ServicesUnits);
}

void ServiceManagerBackend::startUnitFilesQuery()
{
    QStringList args;
    if (m_userScope)
        args << QStringLiteral("--user");
    args << QStringLiteral("list-unit-files")
         << QStringLiteral("--type=service")
         << QStringLiteral("--output=json")
         << QStringLiteral("--no-pager");

    if (!startProcess(args, Task::ServicesFiles))
        finish(QStringLiteral("error"), tr("Impossibile leggere lo stato di avvio dei servizi."));
}

bool ServiceManagerBackend::refreshFailedUnits(bool userScope)
{
    if (m_busy)
        return false;
    m_userScope = userScope;

    QStringList args;
    if (userScope)
        args << QStringLiteral("--user");
    args << QStringLiteral("--failed")
         << QStringLiteral("--type=service")
         << QStringLiteral("--output=json")
         << QStringLiteral("--no-pager");
    return startProcess(args, Task::FailedUnits);
}

bool ServiceManagerBackend::loadJournal(const QString &unit, bool userScope,
                                        const QString &priority)
{
    if (m_busy)
        return false;
    if (!unit.trimmed().isEmpty() && !validJournalUnit(unit.trimmed())) {
        finish(QStringLiteral("error"), tr("Nome unità non valido."));
        return false;
    }

    static const QStringList priorities = {
        QStringLiteral("info"),
        QStringLiteral("warning"),
        QStringLiteral("err")
    };
    const QString safePriority = priorities.contains(priority)
        ? priority : QStringLiteral("warning");

    const QString executable = QStandardPaths::findExecutable(QStringLiteral("journalctl"));
    if (executable.isEmpty()) {
        finish(QStringLiteral("error"), tr("journalctl non è disponibile."));
        return false;
    }

    m_userScope = userScope;
    auto *runner = new ProcessRunner(this);
    m_runner = runner;
    m_busy = true;
    m_state = QStringLiteral("running");
    m_message.clear();
    m_journalText.clear();
    emit stateChanged();

    connect(runner, &ProcessRunner::finished, this,
            [this, runner](ProcessRunner::Outcome outcome, int exitCode,
                           const QByteArray &standardOutput,
                           const QByteArray &standardError,
                           const QString &errorString) {
        if (runner != m_runner)
            return;
        const bool truncated = runner->outputTruncated();
        m_runner = nullptr;
        runner->deleteLater();
        m_busy = false;
        handleFinished(Task::Journal, exitCode, int(outcome),
                       standardOutput, standardError, errorString, truncated);
    });

    QStringList args;
    if (userScope)
        args << QStringLiteral("--user");
    args << QStringLiteral("-b")
         << QStringLiteral("-p") << safePriority
         << QStringLiteral("--no-pager")
         << QStringLiteral("-n") << QStringLiteral("160")
         << QStringLiteral("-o") << QStringLiteral("short-iso");
    if (!unit.trimmed().isEmpty())
        args << QStringLiteral("-u") << unit.trimmed();

    ProcessRunner::Options options;
    options.program = executable;
    options.arguments = args;
    options.timeoutMs = 15000;
    options.maxOutputBytes = kMaxOutput;
    options.mergedChannels = false;
    options.processGroup = true;
    if (!runner->start(options)) {
        m_runner = nullptr;
        runner->deleteLater();
        m_busy = false;
        finish(QStringLiteral("error"), tr("Impossibile avviare journalctl."));
        return false;
    }
    return true;
}

bool ServiceManagerBackend::controlUnit(const QString &unit, bool userScope,
                                        const QString &action)
{
    if (m_busy || !validUnit(unit))
        return false;

    static const QStringList allowedActions = {
        QStringLiteral("start"),
        QStringLiteral("stop"),
        QStringLiteral("restart"),
        QStringLiteral("enable"),
        QStringLiteral("disable"),
        QStringLiteral("reset-failed")
    };
    if (!allowedActions.contains(action)) {
        finish(QStringLiteral("error"), tr("Operazione servizio non valida."));
        return false;
    }

    m_userScope = userScope;
    m_controlAction = action;
    m_controlUnit = unit;

    QStringList args;
    if (userScope)
        args << QStringLiteral("--user");
    args << action << unit;
    return startProcess(args, Task::Control, 60000);
}

void ServiceManagerBackend::handleFinished(Task task, int exitCode, int outcome,
                                           const QByteArray &stdoutData,
                                           const QByteArray &stderrData,
                                           const QString &errorString,
                                           bool outputTruncated)
{
    const auto processResult = static_cast<ProcessRunner::Outcome>(outcome);
    QByteArray combined = stdoutData;
    if (!stderrData.isEmpty()) {
        if (!combined.isEmpty() && !combined.endsWith('\n'))
            combined.append('\n');
        combined.append(stderrData);
    }
    const QString text = QString::fromUtf8(combined).trimmed();

    if (processResult != ProcessRunner::Success) {
        if (task == Task::Control) {
            const bool userScope = m_userScope;
            const QString failedAction = m_controlAction;
            const QString failedUnit = m_controlUnit;
            m_controlAction.clear();
            m_controlUnit.clear();
            OperationLog::append(QStringLiteral("Servizi"),
                                 failedAction + QStringLiteral(" ") + failedUnit,
                                 QStringLiteral("error"), text.left(200));

            QString message;
            QString state = QStringLiteral("error");
            if (processResult == ProcessRunner::Cancelled) {
                state = QStringLiteral("cancelled");
                message = tr("Operazione annullata.");
            } else if (processResult == ProcessRunner::TimedOut) {
                state = QStringLiteral("timeout");
                message = tr("Tempo massimo superato.");
            } else if (processResult == ProcessRunner::FailedToStart) {
                message = tr("Impossibile avviare il comando: %1").arg(errorString);
            } else {
                message = text.isEmpty() ? tr("Comando terminato con codice %1.").arg(exitCode) : text;
            }
            // Finalize every mutable field before the external signal. A direct
            // slot may start a new refresh immediately.
            finish(state, message);
            emit controlFinished(userScope, failedUnit, failedAction, false);
            return;
        }

        if (processResult == ProcessRunner::Cancelled)
            finish(QStringLiteral("cancelled"), tr("Operazione annullata."));
        else if (processResult == ProcessRunner::TimedOut)
            finish(QStringLiteral("timeout"), tr("Tempo massimo superato."));
        else if (processResult == ProcessRunner::FailedToStart)
            finish(QStringLiteral("error"), tr("Impossibile avviare il comando: %1").arg(errorString));
        else
            finish(QStringLiteral("error"), text.isEmpty()
                       ? tr("Comando terminato con codice %1.").arg(exitCode) : text);
        return;
    }

    if (outputTruncated && (task == Task::ServicesUnits
                            || task == Task::ServicesFiles
                            || task == Task::FailedUnits)) {
        m_pendingServices.clear();
        finish(QStringLiteral("error"),
               tr("Output JSON systemd troncato: stato non disponibile."));
        return;
    }

    if (task == Task::ServicesUnits) {
        const ServiceJsonResult parsed = ServiceJson::parseUnitList(
            stdoutData, scopeName(m_userScope));
        if (!parsed.ok()) {
            m_pendingServices.clear();
            finish(QStringLiteral("error"),
                   tr("Impossibile interpretare list-units: %1").arg(parsed.error));
            return;
        }
        m_pendingServices = parsed.rows;
        startUnitFilesQuery();
        return;
    }

    if (task == Task::ServicesFiles) {
        const ServiceJsonResult parsed = ServiceJson::parseUnitFiles(
            stdoutData, scopeName(m_userScope));
        if (!parsed.ok()) {
            m_pendingServices.clear();
            finish(QStringLiteral("error"),
                   tr("Impossibile interpretare list-unit-files: %1").arg(parsed.error));
            return;
        }

        QHash<QString, int> existing;
        for (int i = 0; i < m_pendingServices.size(); ++i)
            existing.insert(m_pendingServices.at(i).toMap().value(QStringLiteral("unit")).toString(), i);

        for (const QVariant &value : parsed.rows) {
            const QVariantMap fileRow = value.toMap();
            const QString unit = fileRow.value(QStringLiteral("unit")).toString();
            const auto it = existing.constFind(unit);
            if (it == existing.cend()) {
                QVariantMap row = fileRow;
                row.insert(QStringLiteral("active"), QStringLiteral("not-loaded"));
                m_pendingServices.append(row);
                existing.insert(unit, m_pendingServices.size() - 1);
            } else {
                QVariantMap row = m_pendingServices.at(it.value()).toMap();
                row.insert(QStringLiteral("enabled"), fileRow.value(QStringLiteral("enabled")));
                m_pendingServices[it.value()] = row;
            }
        }

        std::sort(m_pendingServices.begin(), m_pendingServices.end(),
                  [](const QVariant &a, const QVariant &b) {
            return a.toMap().value(QStringLiteral("unit")).toString()
                < b.toMap().value(QStringLiteral("unit")).toString();
        });
        m_services = m_pendingServices;
        m_pendingServices.clear();
        finish(QStringLiteral("success"), tr("%1 servizi caricati.").arg(m_services.size()));
        return;
    }

    if (task == Task::FailedUnits) {
        const ServiceJsonResult parsed = ServiceJson::parseUnitList(
            stdoutData, scopeName(m_userScope));
        if (!parsed.ok()) {
            finish(QStringLiteral("error"),
                   tr("Stato delle unità fallite non disponibile: %1").arg(parsed.error));
            return;
        }
        m_failedUnits = parsed.rows;
        finish(QStringLiteral("success"),
               m_failedUnits.isEmpty()
                   ? tr("Nessuna unità fallita.")
                   : tr("%1 unità fallite.").arg(m_failedUnits.size()));
        return;
    }

    if (task == Task::Journal) {
        m_journalText = QString::fromUtf8(stdoutData).trimmed();
        finish(QStringLiteral("success"),
               outputTruncated
                   ? tr("Log limitato alla parte più recente disponibile.")
                   : (m_journalText.isEmpty() ? tr("Nessun messaggio corrispondente.") : QString()));
        return;
    }

    if (task == Task::Control) {
        const bool userScope = m_userScope;
        const QString action = m_controlAction;
        const QString unit = m_controlUnit;
        m_controlAction.clear();
        m_controlUnit.clear();
        OperationLog::append(QStringLiteral("Servizi"),
                             action + QStringLiteral(" ") + unit,
                             QStringLiteral("success"));
        finish(QStringLiteral("success"), tr("Operazione completata su %1.").arg(unit));
        emit controlFinished(userScope, unit, action, true);
        return;
    }

    finish(QStringLiteral("success"));
}

bool ServiceManagerBackend::cancel()
{
    return m_runner && m_runner->cancel();
}
