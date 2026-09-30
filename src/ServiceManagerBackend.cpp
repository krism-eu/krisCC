#include "ServiceManagerBackend.h"

#include "OperationLog.h"
#include "ProcessRunner.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
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

QVariantList ServiceManagerBackend::parseUnitsJson(const QByteArray &data,
                                                   const QString &scope) const
{
    QVariantList rows;
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(data, &error);
    if (error.error != QJsonParseError::NoError || !document.isArray())
        return rows;

    for (const QJsonValue &value : document.array()) {
        if (!value.isObject())
            continue;
        const QJsonObject object = value.toObject();
        QString unit = object.value(QStringLiteral("unit")).toString();
        if (unit.isEmpty())
            unit = object.value(QStringLiteral("unit_file")).toString();
        if (!validUnit(unit))
            continue;

        QVariantMap row;
        row.insert(QStringLiteral("unit"), unit);
        row.insert(QStringLiteral("scope"), scope);
        row.insert(QStringLiteral("active"), object.value(QStringLiteral("active")).toString());
        row.insert(QStringLiteral("sub"), object.value(QStringLiteral("sub")).toString());
        row.insert(QStringLiteral("description"), object.value(QStringLiteral("description")).toString());
        row.insert(QStringLiteral("enabled"), object.value(QStringLiteral("state")).toString());
        rows.append(row);
    }
    return rows;
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
        m_runner = nullptr;
        runner->deleteLater();
        m_busy = false;
        handleFinished(task, exitCode, int(outcome),
                       standardOutput, standardError, errorString);
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
        m_runner = nullptr;
        runner->deleteLater();
        m_busy = false;
        handleFinished(Task::Journal, exitCode, int(outcome),
                       standardOutput, standardError, errorString);
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
                                           const QString &errorString)
{
    const auto result = static_cast<ProcessRunner::Outcome>(outcome);
    QByteArray combined = stdoutData;
    if (!stderrData.isEmpty()) {
        if (!combined.isEmpty() && !combined.endsWith('\n'))
            combined.append('\n');
        combined.append(stderrData);
    }
    const QString text = QString::fromUtf8(combined).trimmed();

    if (result != ProcessRunner::Success) {
        if (task == Task::Control) {
            OperationLog::append(QStringLiteral("Servizi"),
                                 m_controlAction + QStringLiteral(" ") + m_controlUnit,
                                 QStringLiteral("error"), text.left(200));
            const QString failedAction = m_controlAction;
            const QString failedUnit = m_controlUnit;
            m_controlAction.clear();
            m_controlUnit.clear();
            emit controlFinished(m_userScope, failedUnit, failedAction, false);
        }
        if (result == ProcessRunner::Cancelled)
            finish(QStringLiteral("cancelled"), tr("Operazione annullata."));
        else if (result == ProcessRunner::TimedOut)
            finish(QStringLiteral("timeout"), tr("Tempo massimo superato."));
        else if (result == ProcessRunner::FailedToStart)
            finish(QStringLiteral("error"), tr("Impossibile avviare il comando: %1").arg(errorString));
        else
            finish(QStringLiteral("error"),
                   text.isEmpty() ? tr("Comando terminato con codice %1.").arg(exitCode) : text);
        return;
    }

    if (task == Task::ServicesUnits) {
        m_pendingServices = parseUnitsJson(stdoutData, scopeName(m_userScope));
        startUnitFilesQuery();
        return;
    }

    if (task == Task::ServicesFiles) {
        const QVariantList unitFiles = parseUnitsJson(stdoutData, scopeName(m_userScope));
        QHash<QString, QString> enabled;
        for (const QVariant &value : unitFiles) {
            const QVariantMap row = value.toMap();
            enabled.insert(row.value(QStringLiteral("unit")).toString(),
                           row.value(QStringLiteral("enabled")).toString());
        }

        for (QVariant &value : m_pendingServices) {
            QVariantMap row = value.toMap();
            row.insert(QStringLiteral("enabled"),
                       enabled.value(row.value(QStringLiteral("unit")).toString(),
                                     QStringLiteral("unknown")));
            value = row;
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
        m_failedUnits = parseUnitsJson(stdoutData, scopeName(m_userScope));
        finish(QStringLiteral("success"),
               m_failedUnits.isEmpty()
                   ? tr("Nessuna unità fallita.")
                   : tr("%1 unità fallite.").arg(m_failedUnits.size()));
        return;
    }

    if (task == Task::Journal) {
        m_journalText = QString::fromUtf8(stdoutData).trimmed();
        finish(QStringLiteral("success"),
               m_journalText.isEmpty() ? tr("Nessun messaggio corrispondente.") : QString());
        return;
    }

    if (task == Task::Control) {
        OperationLog::append(QStringLiteral("Servizi"),
                             m_controlAction + QStringLiteral(" ") + m_controlUnit,
                             QStringLiteral("success"));
        const QString action = m_controlAction;
        const QString unit = m_controlUnit;
        m_controlAction.clear();
        m_controlUnit.clear();
        emit controlFinished(m_userScope, unit, action, true);
        finish(QStringLiteral("success"), tr("Operazione completata su %1.").arg(unit));
        return;
    }

    finish(QStringLiteral("success"));
}

bool ServiceManagerBackend::cancel()
{
    return m_runner && m_runner->cancel();
}
