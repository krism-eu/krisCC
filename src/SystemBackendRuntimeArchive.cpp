#include "SystemBackendRuntime.h"

#include "OperationLog.h"
#include "ProcessRunner.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

namespace {
constexpr int kVerifyTimeoutMs = 10 * 60 * 1000;
constexpr int kRestoreTimeoutMs = 30 * 60 * 1000;

struct HelperResult {
    bool parsed = false;
    bool success = false;
    bool partial = false;
    int applied = 0;
    int members = 0;
    QString message;
};

HelperResult parseResult(const QByteArray &data)
{
    HelperResult result;
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(data.trimmed(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return result;
    const QJsonObject object = document.object();
    if (!object.value(QStringLiteral("success")).isBool()
        || !object.value(QStringLiteral("message")).isString())
        return result;
    result.parsed = true;
    result.success = object.value(QStringLiteral("success")).toBool();
    result.partial = object.value(QStringLiteral("partial")).toBool(false);
    result.applied = object.value(QStringLiteral("applied")).toInt(0);
    result.members = object.value(QStringLiteral("members")).toInt(0);
    result.message = object.value(QStringLiteral("message")).toString();
    return result;
}
}

QString SystemBackendRuntime::archiveHelperPath() const
{
    const QString installed = QStringLiteral("/usr/libexec/kriscc/archive");
    if (QFileInfo(installed).isExecutable())
        return installed;
    const QString local = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("archive"));
    return QFileInfo(local).isExecutable() ? local : QString();
}

bool SystemBackendRuntime::prepareBackupWorkspace(QString *error)
{
    clearBackupWorkspace();
    auto workspace = std::make_unique<QTemporaryDir>(
        QDir::tempPath() + QStringLiteral("/kriscc-operation-XXXXXX"));
    if (!workspace->isValid()) {
        if (error)
            *error = tr("Impossibile creare il workspace privato dell'operazione.");
        return false;
    }
    workspace->setAutoRemove(true);
    if (!QFile::setPermissions(workspace->path(), QFileDevice::ReadOwner
                                                  | QFileDevice::WriteOwner
                                                  | QFileDevice::ExeOwner)) {
        if (error)
            *error = tr("Impossibile proteggere il workspace dell'operazione.");
        return false;
    }
    m_backupWorkspace = std::move(workspace);
    return true;
}

void SystemBackendRuntime::clearBackupWorkspace()
{
    m_backupWorkspace.reset();
}

bool SystemBackendRuntime::startCreatedArchiveValidation(const QString &output,
                                                         const QString &partial)
{
    const QString helper = archiveHelperPath();
    QString workspaceError;
    if (helper.isEmpty() || !prepareBackupWorkspace(&workspaceError)) {
        setBackupResult(helper.isEmpty()
                            ? tr("Helper strutturato degli archivi non disponibile.")
                            : workspaceError,
                        QString(), QStringLiteral("error"));
        return false;
    }

    auto *validator = new ProcessRunner(this);
    m_backupRunner = validator;
    setBackupResult(tr("Validazione strutturata dello snapshot in corso…"),
                    output, QStringLiteral("running"));
    connect(validator, &ProcessRunner::finished, this,
            [this, validator, output, partial](ProcessRunner::Outcome outcome, int,
                                               const QByteArray &stdoutData,
                                               const QByteArray &stderrData,
                                               const QString &errorString) {
        if (validator != m_backupRunner)
            return;
        m_backupRunner = nullptr;
        validator->deleteLater();
        const HelperResult parsed = parseResult(stdoutData);
        clearBackupWorkspace();

        if (outcome == ProcessRunner::Success && parsed.parsed && parsed.success) {
            if (QFileInfo::exists(output) && !QFile::remove(output)) {
                m_backupPartialPath = partial;
                setBackupBusy(false);
                setBackupResult(tr("Snapshot valido ma il nome finale è già occupato e non può essere sostituito."),
                                partial, QStringLiteral("error"));
                return;
            }
            if (!QFile::rename(partial, output)) {
                m_backupPartialPath = partial;
                setBackupBusy(false);
                setBackupResult(tr("Snapshot valido ma non è stato possibile finalizzarne il nome. Il file parziale è stato conservato."),
                                partial, QStringLiteral("error"));
                return;
            }
            m_backupPartialPath.clear();
            QFile::setPermissions(output, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
            setBackupBusy(false);
            setBackupResult(tr("Snapshot creato e validato correttamente."), output,
                            QStringLiteral("success"));
            OperationLog::append(QStringLiteral("Backup"), QStringLiteral("create"),
                                 QStringLiteral("success"), QFileInfo(output).fileName());
            notify(tr("Backup completato"), output);
            return;
        }

        QFile::remove(partial);
        m_backupPartialPath.clear();
        setBackupBusy(false);
        QString message = parsed.parsed ? parsed.message : QString::fromUtf8(stderrData).trimmed();
        if (message.isEmpty())
            message = errorString.isEmpty()
                ? tr("Lo snapshot contiene elementi non supportati dal ripristino sicuro.")
                : errorString;
        setBackupResult(message, QString(), QStringLiteral("error"));
        OperationLog::append(QStringLiteral("Backup"), QStringLiteral("create"),
                             QStringLiteral("error"), QFileInfo(output).fileName());
    });

    ProcessRunner::Options options;
    options.program = helper;
    options.arguments = {QStringLiteral("validate-created"), partial,
                         m_backupWorkspace->path()};
    options.timeoutMs = kVerifyTimeoutMs;
    options.maxOutputBytes = 64 * 1024;
    options.mergedChannels = false;
    options.processGroup = true;
    if (!validator->start(options)) {
        m_backupRunner = nullptr;
        validator->deleteLater();
        clearBackupWorkspace();
        setBackupResult(tr("Impossibile avviare la validazione strutturata dello snapshot."),
                        QString(), QStringLiteral("error"));
        return false;
    }
    return true;
}

bool SystemBackendRuntime::verifySnapshot(const QString &path)
{
    if (m_backupBusy)
        return false;
    const QString helper = archiveHelperPath();
    if (helper.isEmpty()) {
        setBackupResult(tr("Helper strutturato degli archivi non disponibile."), QString(), QStringLiteral("error"));
        return false;
    }
    QString workspaceError;
    if (!prepareBackupWorkspace(&workspaceError)) {
        setBackupResult(workspaceError, QString(), QStringLiteral("error"));
        return false;
    }

    auto *runner = new ProcessRunner(this);
    m_backupRunner = runner;
    setBackupBusy(true);
    setBackupResult(tr("Verifica strutturata archivio in corso…"), path, QStringLiteral("running"));
    connect(runner, &ProcessRunner::finished, this,
            [this, runner, path](ProcessRunner::Outcome outcome, int,
                                 const QByteArray &stdoutData, const QByteArray &stderrData,
                                 const QString &errorString) {
        if (runner != m_backupRunner)
            return;
        m_backupRunner = nullptr;
        runner->deleteLater();
        const HelperResult parsed = parseResult(stdoutData);
        clearBackupWorkspace();
        setBackupBusy(false);

        if (outcome == ProcessRunner::Success && parsed.parsed && parsed.success) {
            setBackupResult(tr("Archivio verificato correttamente (%1 membri).").arg(parsed.members),
                            path, QStringLiteral("success"));
            OperationLog::append(QStringLiteral("Backup"), QStringLiteral("verify"),
                                 QStringLiteral("success"), QFileInfo(path).fileName());
            return;
        }

        QString message = parsed.parsed ? parsed.message : QString::fromUtf8(stderrData).trimmed();
        QString state = QStringLiteral("error");
        if (outcome == ProcessRunner::Cancelled) {
            message = tr("Verifica annullata.");
            state = QStringLiteral("cancelled");
        } else if (outcome == ProcessRunner::TimedOut) {
            message = tr("Tempo massimo superato durante la verifica dell'archivio.");
        } else if (outcome == ProcessRunner::FailedToStart) {
            message = tr("Impossibile avviare la verifica: %1").arg(errorString);
        } else if (message.isEmpty()) {
            message = tr("Archivio non valido, non supportato o modificato durante la verifica.");
        }
        setBackupResult(message, path, state);
        OperationLog::append(QStringLiteral("Backup"), QStringLiteral("verify"), state,
                             QFileInfo(path).fileName());
    });

    ProcessRunner::Options options;
    options.program = helper;
    options.arguments = {QStringLiteral("validate"), currentBackupRoot(), path,
                         m_backupWorkspace->path()};
    options.timeoutMs = kVerifyTimeoutMs;
    options.maxOutputBytes = 64 * 1024;
    options.mergedChannels = false;
    options.processGroup = true;
    if (!runner->start(options)) {
        m_backupRunner = nullptr;
        runner->deleteLater();
        clearBackupWorkspace();
        setBackupBusy(false);
        setBackupResult(tr("Impossibile inizializzare la verifica dell'archivio."), path,
                        QStringLiteral("error"));
        return false;
    }
    return true;
}

bool SystemBackendRuntime::restoreSnapshot(const QString &path)
{
    if (m_backupBusy)
        return false;
    const QString helper = archiveHelperPath();
    if (helper.isEmpty()) {
        setBackupResult(tr("Helper strutturato degli archivi non disponibile."), QString(), QStringLiteral("error"));
        return false;
    }
    QString workspaceError;
    if (!prepareBackupWorkspace(&workspaceError)) {
        setBackupResult(workspaceError, QString(), QStringLiteral("error"));
        return false;
    }

    auto *runner = new ProcessRunner(this);
    m_backupRunner = runner;
    setBackupBusy(true);
    setBackupResult(tr("Ripristino confinato in corso…"), path, QStringLiteral("running"));
    connect(runner, &ProcessRunner::finished, this,
            [this, runner, path](ProcessRunner::Outcome outcome, int,
                                 const QByteArray &stdoutData, const QByteArray &stderrData,
                                 const QString &errorString) {
        if (runner != m_backupRunner)
            return;
        m_backupRunner = nullptr;
        runner->deleteLater();
        const HelperResult parsed = parseResult(stdoutData);
        clearBackupWorkspace();
        setBackupBusy(false);

        if (outcome == ProcessRunner::Success && parsed.parsed && parsed.success) {
            setBackupResult(tr("Backup ripristinato in modo confinato (%1 membri applicati). Disconnettersi o riavviare le applicazioni interessate per applicare tutte le configurazioni.")
                                .arg(parsed.applied),
                            path, QStringLiteral("success"));
            OperationLog::append(QStringLiteral("Backup"), QStringLiteral("restore"),
                                 QStringLiteral("success"), QFileInfo(path).fileName());
            notify(tr("Ripristino completato"), QFileInfo(path).fileName());
            return;
        }

        QString message = parsed.parsed ? parsed.message : QString::fromUtf8(stderrData).trimmed();
        QString state = parsed.partial ? QStringLiteral("warning") : QStringLiteral("error");
        if (outcome == ProcessRunner::Cancelled) {
            message = tr("Ripristino annullato. Alcuni file potrebbero essere già stati applicati; il workspace temporaneo è stato bonificato.");
            state = QStringLiteral("warning");
        } else if (outcome == ProcessRunner::TimedOut) {
            message = tr("Ripristino interrotto per timeout. Alcuni file potrebbero essere già stati applicati; il workspace temporaneo è stato bonificato.");
            state = QStringLiteral("warning");
        } else if (outcome == ProcessRunner::FailedToStart) {
            message = tr("Impossibile avviare il ripristino: %1").arg(errorString);
        } else if (message.isEmpty()) {
            message = tr("Ripristino non riuscito.");
        }
        setBackupResult(message, path, state);
        OperationLog::append(QStringLiteral("Backup"), QStringLiteral("restore"), state,
                             QFileInfo(path).fileName());
    });

    ProcessRunner::Options options;
    options.program = helper;
    options.arguments = {QStringLiteral("restore"), currentBackupRoot(), path,
                         QDir::homePath(), m_backupWorkspace->path()};
    options.timeoutMs = kRestoreTimeoutMs;
    options.maxOutputBytes = 64 * 1024;
    options.mergedChannels = false;
    options.processGroup = true;
    if (!runner->start(options)) {
        m_backupRunner = nullptr;
        runner->deleteLater();
        clearBackupWorkspace();
        setBackupBusy(false);
        setBackupResult(tr("Impossibile inizializzare il ripristino."), path,
                        QStringLiteral("error"));
        return false;
    }
    return true;
}
