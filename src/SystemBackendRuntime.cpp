#include "SystemBackendRuntime.h"

#include "BackupSafety.h"
#include "OperationLog.h"
#include "ProcessRunner.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStorageInfo>

namespace {
constexpr int kBackupOperationTimeoutMs = 30 * 60 * 1000;

QString humanGiB(quint64 bytes)
{
    return QString::number(double(bytes) / (1024.0 * 1024.0 * 1024.0), 'f', 1)
         + QStringLiteral(" GiB");
}

const QStringList &configEntries()
{
    static const QStringList entries = {
        QStringLiteral(".config"),
        QStringLiteral(".local/share/applications"),
        QStringLiteral(".local/share/konsole"),
        QStringLiteral(".local/share/kxmlgui5"),
        QStringLiteral(".local/share/plasma"),
        QStringLiteral(".local/share/kwin")
    };
    return entries;
}

const QStringList &homeExcludes()
{
    static const QStringList entries = {
        QStringLiteral(".cache"),
        QStringLiteral(".local/share/Trash"),
        QStringLiteral(".local/share/flatpak"),
        QStringLiteral(".local/share/containers"),
        QStringLiteral(".var/app/*/cache"),
        QStringLiteral("krisCC Backups"),
        QStringLiteral("KCC Backups"),
        QStringLiteral("K-ControlC Backups")
    };
    return entries;
}
}

QVariantList SystemBackendRuntime::backups() const
{
    const QString root = currentBackupRoot();
    if (root.isEmpty())
        return {};
    QString ignoredError;
    return BackupSafety::listBackups(root, &ignoredError);
}

bool SystemBackendRuntime::deleteSnapshot(const QString &path)
{
    if (m_backupBusy) {
        setBackupResult(tr("Attendere il completamento dell'operazione di backup in corso."),
                        QString(), QStringLiteral("error"));
        return false;
    }

    QString name;
    QString error;
    if (!BackupSafety::removeBackup(currentBackupRoot(), path, &name, &error)) {
        setBackupResult(error.isEmpty() ? tr("Archivio di backup non valido.") : error,
                        QString(), QStringLiteral("error"));
        return false;
    }

    OperationLog::append(QStringLiteral("Backup"), QStringLiteral("delete"),
                         QStringLiteral("success"), name);
    setBackupResult(tr("Backup eliminato: %1.").arg(name), QString(), QStringLiteral("success"));
    return true;
}

bool SystemBackendRuntime::appendBackupDestinationExclusions(const QString &kind,
                                                             const QString &home,
                                                             const QString &backupRoot,
                                                             const QString &output,
                                                             const QString &partial,
                                                             QStringList *arguments,
                                                             QString *error) const
{
    Q_UNUSED(output)
    Q_UNUSED(partial)
    if (!arguments)
        return false;

    const QString canonicalHome = QFileInfo(home).canonicalFilePath();
    if (canonicalHome.isEmpty())
        return true;

    if (backupRoot == canonicalHome) {
        if (kind == QStringLiteral("home")) {
            *arguments << QStringLiteral("--exclude=./config-*.tar.gz")
                       << QStringLiteral("--exclude=./home-*.tar.gz")
                       << QStringLiteral("--exclude=./config-*.tar.gz.partial")
                       << QStringLiteral("--exclude=./home-*.tar.gz.partial");
        }
        return true;
    }

    if (!backupRoot.startsWith(canonicalHome + QLatin1Char('/')))
        return true;

    const QString relative = QDir(canonicalHome).relativeFilePath(backupRoot);
    if (relative.isEmpty() || relative == QStringLiteral("."))
        return true;

    if (kind == QStringLiteral("home")) {
        *arguments << QStringLiteral("--exclude=./") + relative;
        return true;
    }

    bool insideIncludedRoot = false;
    for (const QString &entry : configEntries()) {
        if (relative == entry) {
            if (error) {
                *error = tr("La cartella backup coincide con una radice inclusa nel profilo configurazione (%1). Scegliere una sottocartella dedicata o una destinazione esterna.")
                             .arg(entry);
            }
            return false;
        }
        if (relative.startsWith(entry + QLatin1Char('/'))) {
            insideIncludedRoot = true;
            break;
        }
    }

    if (insideIncludedRoot)
        *arguments << QStringLiteral("--exclude=") + relative;
    return true;
}

bool SystemBackendRuntime::createSnapshot(const QString &kind)
{
    if (m_backupBusy)
        return false;
    if (kind != QStringLiteral("home") && kind != QStringLiteral("config")) {
        setBackupResult(tr("Tipo di snapshot non consentito."), QString(), QStringLiteral("error"));
        return false;
    }
    if (archiveHelperPath().isEmpty()) {
        setBackupResult(tr("Helper strutturato degli archivi non disponibile."),
                        QString(), QStringLiteral("error"));
        return false;
    }

    const QString tar = resolveExecutable(QStringLiteral("tar"));
    if (tar.isEmpty()) {
        setBackupResult(tr("tar non disponibile."), QString(), QStringLiteral("error"));
        return false;
    }

    const QString home = QDir::homePath();
    QDir backupDir(m_backupDirectory.isEmpty() ? defaultBackupDirectory() : m_backupDirectory);
    if (!backupDir.exists() && !backupDir.mkpath(QStringLiteral("."))) {
        setBackupResult(tr("Impossibile creare la cartella dei backup."), QString(), QStringLiteral("error"));
        return false;
    }

    QString canonicalBackupRoot;
    if (!validateBackupDirectory(backupDir.absolutePath(), &canonicalBackupRoot)) {
        setBackupResult(tr("La cartella backup selezionata non è disponibile o scrivibile."),
                        QString(), QStringLiteral("error"));
        return false;
    }
    if (m_backupDirectory != canonicalBackupRoot) {
        m_backupDirectory = canonicalBackupRoot;
        QSettings settings;
        settings.setValue(QStringLiteral("backup/directory"), m_backupDirectory);
        emit backupDirectoryChanged();
    }
    backupDir.setPath(canonicalBackupRoot);

    const QStorageInfo backupStorage(backupDir.absolutePath());
    if (backupStorage.isValid() && backupStorage.isReady()) {
        const qint64 oneGiB = 1024LL * 1024LL * 1024LL;
        const qint64 minimumFree = kind == QStringLiteral("home") ? 5LL * oneGiB : oneGiB;
        if (backupStorage.bytesAvailable() < minimumFree) {
            setBackupResult(tr("Spazio libero insufficiente per lo snapshot: disponibili %1, richiesti almeno %2.")
                                .arg(humanGiB(quint64(backupStorage.bytesAvailable())),
                                     humanGiB(quint64(minimumFree))),
                            QString(), QStringLiteral("error"));
            return false;
        }
    }

    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    const QString label = kind == QStringLiteral("home") ? QStringLiteral("home") : QStringLiteral("config");
    const QString output = backupDir.filePath(QStringLiteral("%1-%2.tar.gz").arg(label, stamp));
    const QString partial = output + QStringLiteral(".partial");

    QStringList args = {QStringLiteral("-czf"), partial};
    if (kind == QStringLiteral("home")) {
        for (const QString &excluded : homeExcludes())
            args << QStringLiteral("--exclude=./") + excluded;
    }

    QString exclusionError;
    if (!appendBackupDestinationExclusions(kind, home, canonicalBackupRoot,
                                           output, partial, &args, &exclusionError)) {
        setBackupResult(exclusionError, QString(), QStringLiteral("error"));
        return false;
    }

    if (kind == QStringLiteral("home")) {
        args << QStringLiteral("-C") << home << QStringLiteral(".");
    } else {
        QStringList entries;
        for (const QString &candidate : configEntries()) {
            if (QFileInfo::exists(home + QLatin1Char('/') + candidate))
                entries << candidate;
        }
        if (entries.isEmpty()) {
            setBackupResult(tr("Nessuna cartella di configurazione trovata."), QString(), QStringLiteral("error"));
            return false;
        }
        args << QStringLiteral("-C") << home;
        args << entries;
    }

    QFile::remove(partial);
    {
        QFile partialFile(partial);
        if (!partialFile.open(QIODevice::WriteOnly | QIODevice::NewOnly)
            || !partialFile.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
            setBackupResult(tr("Impossibile creare il file parziale del backup con permessi sicuri."),
                            QString(), QStringLiteral("error"));
            return false;
        }
    }

    auto *runner = new ProcessRunner(this);
    m_backupRunner = runner;
    m_backupPartialPath = partial;
    setBackupBusy(true);
    setBackupResult(tr("Creazione snapshot in corso…"), output, QStringLiteral("running"));

    connect(runner, &ProcessRunner::finished, this,
            [this, runner, output, partial](ProcessRunner::Outcome outcome, int exitCode,
                                            const QByteArray &stdoutData,
                                            const QByteArray &stderrData,
                                            const QString &errorString) {
        if (runner != m_backupRunner)
            return;
        m_backupRunner = nullptr;
        runner->deleteLater();

        QByteArray combined = stdoutData;
        if (!combined.isEmpty() && !stderrData.isEmpty() && !combined.endsWith('\n'))
            combined.append('\n');
        combined.append(stderrData);
        const QString details = QString::fromUtf8(combined).trimmed();
        const bool archiveProduced = QFileInfo(partial).exists() && QFileInfo(partial).size() > 0;

        if (outcome == ProcessRunner::Success && archiveProduced) {
            if (!startCreatedArchiveValidation(output, partial)) {
                QFile::remove(partial);
                m_backupPartialPath.clear();
                setBackupBusy(false);
            }
            return;
        }

        QFile::remove(partial);
        m_backupPartialPath.clear();
        setBackupBusy(false);
        QString message;
        QString state = QStringLiteral("error");
        if (outcome == ProcessRunner::Cancelled) {
            message = tr("Backup annullato; il file parziale è stato rimosso.");
            state = QStringLiteral("cancelled");
        } else if (outcome == ProcessRunner::TimedOut) {
            message = tr("Tempo massimo superato durante il backup; il file parziale è stato rimosso.");
        } else if (outcome == ProcessRunner::FailedToStart) {
            message = tr("Impossibile avviare il backup: %1").arg(errorString);
        } else if (!archiveProduced && outcome == ProcessRunner::Success) {
            message = tr("Snapshot terminato senza produrre un archivio utilizzabile.");
        } else {
            message = details.isEmpty()
                ? tr("Snapshot non riuscito (codice %1).").arg(exitCode)
                : details;
        }
        setBackupResult(message, QString(), state);
        OperationLog::append(QStringLiteral("Backup"), QStringLiteral("create"),
                             outcome == ProcessRunner::TimedOut ? QStringLiteral("timeout") : state,
                             QFileInfo(output).fileName());
    });

    ProcessRunner::Options options;
    options.program = tar;
    options.arguments = args;
    options.timeoutMs = kBackupOperationTimeoutMs;
    options.maxOutputBytes = 256 * 1024;
    options.mergedChannels = false;
    options.processGroup = true;
    if (!runner->start(options)) {
        m_backupRunner = nullptr;
        runner->deleteLater();
        QFile::remove(partial);
        m_backupPartialPath.clear();
        setBackupBusy(false);
        setBackupResult(tr("Impossibile inizializzare il backup."), QString(),
                        QStringLiteral("error"));
        return false;
    }
    return true;
}

bool SystemBackendRuntime::cancelSnapshot()
{
    return m_backupRunner && m_backupBusy && m_backupRunner->cancel();
}
