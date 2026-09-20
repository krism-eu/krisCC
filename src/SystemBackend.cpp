#include "SystemBackend.h"

#include "OperationLog.h"

#include <QClipboard>
#include <QDateTime>
#include <QDBusInterface>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusReply>
#include <QDBusVariant>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLocale>
#include <QHash>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QSysInfo>
#include <QTextStream>
#include <QTimeZone>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>

#include <sys/sysinfo.h>

namespace {
QString humanGiB(quint64 bytes)
{
    return QString::number(double(bytes) / (1024.0 * 1024.0 * 1024.0), 'f', 1)
         + QStringLiteral(" GiB");
}

QString systemTimeZoneName()
{
    const QByteArray id = QTimeZone::systemTimeZoneId();
    return id.isEmpty() ? QStringLiteral("UTC") : QString::fromUtf8(id);
}

const QSet<QString> &allowedServices()
{
    static const QSet<QString> services = {
        QStringLiteral("NetworkManager.service"),
        QStringLiteral("cups.service"),
        QStringLiteral("bluetooth.service")
    };
    return services;
}

const QStringList &backupConfigEntries()
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

const QStringList &backupHomeExcludes()
{
    static const QStringList entries = {
        QStringLiteral(".cache"),
        QStringLiteral(".local/share/Trash"),
        QStringLiteral(".local/share/flatpak"),
        QStringLiteral(".local/share/containers"),
        QStringLiteral("krisCC Backups"),
        QStringLiteral("KCC Backups"),
        QStringLiteral("K-ControlC Backups")
    };
    return entries;
}
}

SystemBackend::SystemBackend(QObject *parent)
    : QObject(parent)
{
}

SystemBackend::~SystemBackend()
{
    if (m_backupProcess && m_backupProcess->state() != QProcess::NotRunning) {
        m_backupProcess->kill();
        m_backupProcess->waitForFinished(2000);
    }
    if (!m_backupPartialPath.isEmpty())
        QFile::remove(m_backupPartialPath);
}

QString SystemBackend::osName() const
{
    return readOsName();
}

QString SystemBackend::kernelVersion() const
{
    return QSysInfo::kernelType() + QStringLiteral(" ") + QSysInfo::kernelVersion();
}

QString SystemBackend::architecture() const
{
    return QSysInfo::currentCpuArchitecture();
}

QString SystemBackend::hostName() const
{
    return QSysInfo::machineHostName();
}

QString SystemBackend::memorySummary() const
{
    struct sysinfo info {};
    if (::sysinfo(&info) == 0) {
        const quint64 total = quint64(info.totalram) * quint64(info.mem_unit);
        if (total > 0)
            return humanGiB(total);
    }

    QFile file(QStringLiteral("/proc/meminfo"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return tr("Non disponibile");

    while (!file.atEnd()) {
        const QByteArray line = file.readLine().simplified();
        if (!line.startsWith("MemTotal:"))
            continue;
        const QList<QByteArray> parts = line.split(' ');
        if (parts.size() >= 2) {
            bool ok = false;
            const quint64 kib = parts.at(1).toULongLong(&ok);
            if (ok)
                return humanGiB(kib * 1024ULL);
        }
    }
    return tr("Non disponibile");
}

QString SystemBackend::storageSummary() const
{
    QStorageInfo storage(QDir::homePath());
    if (!storage.isValid() || !storage.isReady() || storage.bytesTotal() == 0)
        storage = QStorageInfo(QStringLiteral("/var"));
    if (!storage.isValid() || !storage.isReady() || storage.bytesTotal() == 0)
        return tr("Non disponibile");
    return tr("%1 liberi su %2").arg(humanGiB(storage.bytesAvailable()), humanGiB(storage.bytesTotal()));
}

QString SystemBackend::desktopSession() const
{
    const QString desktop = qEnvironmentVariable("XDG_CURRENT_DESKTOP", tr("Desktop sconosciuto"));
    const QString session = qEnvironmentVariable("XDG_SESSION_TYPE", QStringLiteral("?"));
    return desktop + QStringLiteral(" · ") + session;
}

QString SystemBackend::selinuxState() const
{
    const QFileInfo info(QStringLiteral("/sys/fs/selinux/enforce"));
    if (!info.exists())
        return tr("Disabilitato");

    QFile file(info.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return tr("Sconosciuto");

    const QByteArray value = file.readAll().trimmed();
    if (value == "1")
        return tr("Enforcing");
    if (value == "0")
        return tr("Permissive");
    return tr("Sconosciuto");
}

QString SystemBackend::quickSystemInfo() const
{
    QString text;
    QTextStream out(&text);
    out << tr("Informazioni rapide di sistema") << '\n';
    out << tr("Sistema operativo: ") << osName() << '\n';
    out << tr("Host: ") << hostName() << '\n';
    out << tr("Kernel: ") << kernelVersion() << '\n';
    out << tr("Architettura: ") << architecture() << '\n';
    out << tr("RAM: ") << memorySummary() << '\n';
    out << tr("Storage dati: ") << storageSummary() << '\n';
    out << tr("Desktop: ") << desktopSession() << '\n';
    out << tr("SELinux: ") << selinuxState() << '\n';
    out << tr("Fuso orario: ") << systemTimeZoneName() << '\n';
    out << tr("Modalità di avvio: ") << (QFileInfo::exists(QStringLiteral("/sys/firmware/efi")) ? "UEFI" : "BIOS") << '\n';
    out << "Qt: " << qVersion() << '\n';
    return text.trimmed();
}

void SystemBackend::copyToClipboard(const QString &text) const
{
    if (QGuiApplication::clipboard())
        QGuiApplication::clipboard()->setText(text);
}

QString SystemBackend::flatpakIconPath(const QString &appId) const
{
    static const QRegularExpression safeId(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._-]{0,255}$"));
    const QString id = appId.trimmed();
    if (!safeId.match(id).hasMatch())
        return {};

    const QString home = QDir::homePath();
    const QStringList iconRoots = {
        home + QStringLiteral("/.local/share/flatpak/exports/share/icons/hicolor"),
        QStringLiteral("/var/lib/flatpak/exports/share/icons/hicolor"),
        QStringLiteral("/usr/share/icons/hicolor")
    };
    const QStringList iconPaths = {
        QStringLiteral("128x128/apps/") + id + QStringLiteral(".png"),
        QStringLiteral("64x64/apps/") + id + QStringLiteral(".png"),
        QStringLiteral("scalable/apps/") + id + QStringLiteral(".svg")
    };

    for (const QString &root : iconRoots) {
        for (const QString &relative : iconPaths) {
            const QString candidate = QDir(root).filePath(relative);
            if (QFileInfo(candidate).isFile())
                return QUrl::fromLocalFile(candidate).toString();
        }
    }

    const QString arch = QSysInfo::currentCpuArchitecture();
    const QStringList appstreamRoots = {
        home + QStringLiteral("/.local/share/flatpak/appstream"),
        QStringLiteral("/var/lib/flatpak/appstream")
    };
    for (const QString &root : appstreamRoots) {
        QDir appstream(root);
        const QStringList remotes = appstream.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString &remote : remotes) {
            for (const QString &size : {QStringLiteral("128x128"), QStringLiteral("64x64")}) {
                const QString candidate = appstream.filePath(
                    remote + QLatin1Char('/') + arch
                    + QStringLiteral("/active/icons/") + size
                    + QLatin1Char('/') + id + QStringLiteral(".png"));
                if (QFileInfo(candidate).isFile())
                    return QUrl::fromLocalFile(candidate).toString();
            }
        }
    }
    return {};
}

QString SystemBackend::resolveExecutable(const QString &program) const
{
    if (program.isEmpty())
        return {};
    if (program.startsWith(QLatin1Char('/'))) {
        const QFileInfo info(program);
        return info.exists() && info.isExecutable() ? info.absoluteFilePath() : QString();
    }
    const QString found = QStandardPaths::findExecutable(program);
    if (!found.isEmpty())
        return found;
    for (const QString &prefix : {QStringLiteral("/usr/sbin/"), QStringLiteral("/usr/bin/")}) {
        const QFileInfo info(prefix + program);
        if (info.exists() && info.isExecutable())
            return info.absoluteFilePath();
    }
    return {};
}

QString SystemBackend::toolProgram(const QString &toolId) const
{
    static const QHash<QString, QString> names = {
        {QStringLiteral("systemsettings"), QStringLiteral("systemsettings")},
        {QStringLiteral("kinfocenter"), QStringLiteral("kinfocenter")},
        {QStringLiteral("partitionmanager"), QStringLiteral("partitionmanager")},
        {QStringLiteral("discover"), QStringLiteral("plasma-discover")},
        {QStringLiteral("ksystemlog"), QStringLiteral("ksystemlog")},
        {QStringLiteral("systemmonitor"), QStringLiteral("plasma-systemmonitor")},
        {QStringLiteral("qdirstat"), QStringLiteral("qdirstat")},
        {QStringLiteral("konsole"), QStringLiteral("konsole")}
    };
    return resolveExecutable(names.value(toolId));
}

bool SystemBackend::toolAvailable(const QString &toolId) const
{
    return !toolProgram(toolId).isEmpty();
}

bool SystemBackend::launchTool(const QString &toolId) const
{
    const QString program = toolProgram(toolId);
    if (program.isEmpty())
        return false;
    const QFileInfo info(program);
    return info.exists() && info.isExecutable() && QProcess::startDetached(program, {});
}

bool SystemBackend::programAvailable(const QString &program) const
{
    return !resolveExecutable(program).isEmpty();
}

QString SystemBackend::serviceState(const QString &service) const
{
    if (!allowedServices().contains(service))
        return tr("non consentito");

    QDBusInterface manager(QStringLiteral("org.freedesktop.systemd1"),
                           QStringLiteral("/org/freedesktop/systemd1"),
                           QStringLiteral("org.freedesktop.systemd1.Manager"),
                           QDBusConnection::systemBus());
    const QDBusReply<QDBusObjectPath> unitReply = manager.call(QStringLiteral("GetUnit"), service);
    if (!unitReply.isValid())
        return tr("non disponibile");

    QDBusInterface properties(QStringLiteral("org.freedesktop.systemd1"), unitReply.value().path(),
                              QStringLiteral("org.freedesktop.DBus.Properties"),
                              QDBusConnection::systemBus());
    const QDBusReply<QDBusVariant> stateReply = properties.call(
        QStringLiteral("Get"), QStringLiteral("org.freedesktop.systemd1.Unit"), QStringLiteral("ActiveState"));
    return stateReply.isValid() ? stateReply.value().variant().toString() : tr("sconosciuto");
}

bool SystemBackend::restartService(const QString &service)
{
    if (!allowedServices().contains(service))
        return false;

    QDBusInterface manager(QStringLiteral("org.freedesktop.systemd1"),
                           QStringLiteral("/org/freedesktop/systemd1"),
                           QStringLiteral("org.freedesktop.systemd1.Manager"),
                           QDBusConnection::systemBus());
    if (!manager.isValid())
        return false;

    auto *watcher = new QDBusPendingCallWatcher(
        manager.asyncCall(QStringLiteral("RestartUnit"), service, QStringLiteral("replace")), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, service](QDBusPendingCallWatcher *call) {
        const QDBusPendingReply<QDBusObjectPath> reply(*call);
        notify(reply.isError() ? tr("Riavvio servizio non riuscito") : tr("Servizio riavviato"),
               reply.isError() ? reply.error().message() : service);
        call->deleteLater();
    });
    return true;
}

void SystemBackend::requestReboot()
{
    QDBusInterface manager(QStringLiteral("org.freedesktop.login1"),
                           QStringLiteral("/org/freedesktop/login1"),
                           QStringLiteral("org.freedesktop.login1.Manager"),
                           QDBusConnection::systemBus());
    if (!manager.isValid()) {
        emit rebootFinished(false, tr("Il servizio di riavvio logind non è disponibile."));
        return;
    }

    auto *watcher = new QDBusPendingCallWatcher(
        manager.asyncCall(QStringLiteral("Reboot"), true), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this](QDBusPendingCallWatcher *call) {
        const QDBusPendingReply<> reply(*call);
        if (reply.isError())
            emit rebootFinished(false, tr("Riavvio non autorizzato o non riuscito: %1")
                                           .arg(reply.error().message()));
        else
            emit rebootFinished(true, QString());
        call->deleteLater();
    });
}

void SystemBackend::notify(const QString &summary, const QString &body) const
{
    QDBusInterface notifications(QStringLiteral("org.freedesktop.Notifications"),
                                 QStringLiteral("/org/freedesktop/Notifications"),
                                 QStringLiteral("org.freedesktop.Notifications"),
                                 QDBusConnection::sessionBus());
    if (!notifications.isValid())
        return;
    notifications.asyncCall(QStringLiteral("Notify"), QStringLiteral("krisCC"), 0u,
                            QStringLiteral("krisCC"), summary, body,
                            QStringList(), QVariantMap(), 5000);
}

QVariantList SystemBackend::backups() const
{
    QVariantList result;
    const QDir backupDir(QDir::homePath() + QStringLiteral("/krisCC Backups"));
    if (!backupDir.exists())
        return result;

    const QFileInfoList files = backupDir.entryInfoList(
        {QStringLiteral("config-*.tar.gz"), QStringLiteral("home-*.tar.gz")},
        QDir::Files | QDir::Readable, QDir::Time);
    for (const QFileInfo &info : files) {
        QVariantMap item;
        item.insert(QStringLiteral("name"), info.fileName());
        item.insert(QStringLiteral("path"), info.absoluteFilePath());
        item.insert(QStringLiteral("size"), info.size());
        item.insert(QStringLiteral("modified"), info.lastModified().toString(Qt::ISODate));
        item.insert(QStringLiteral("kind"), info.fileName().startsWith(QStringLiteral("home-"))
                                               ? QStringLiteral("home") : QStringLiteral("config"));
        result.append(item);
    }
    return result;
}

bool SystemBackend::validateBackupPath(const QString &path, QString *canonicalPath) const
{
    const QDir backupDir(QDir::homePath() + QStringLiteral("/krisCC Backups"));
    const QString backupRoot = QFileInfo(backupDir.absolutePath()).canonicalFilePath();
    const QFileInfo info(path);
    const QString canonical = info.canonicalFilePath();
    if (backupRoot.isEmpty() || canonical.isEmpty() || !info.isFile())
        return false;
    if (!canonical.startsWith(backupRoot + QLatin1Char('/')))
        return false;

    static const QRegularExpression namePattern(
        QStringLiteral("^(config|home)-[0-9]{8}-[0-9]{6}\\.tar\\.gz$"));
    if (!namePattern.match(info.fileName()).hasMatch())
        return false;

    if (canonicalPath)
        *canonicalPath = canonical;
    return true;
}

bool SystemBackend::verifySnapshot(const QString &path)
{
    if (m_backupBusy)
        return false;

    QString canonical;
    if (!validateBackupPath(path, &canonical)) {
        setBackupResult(tr("Archivio di backup non valido o fuori dalla cartella krisCC Backups."),
                        QString(), QStringLiteral("error"));
        return false;
    }

    const QString tar = resolveExecutable(QStringLiteral("tar"));
    if (tar.isEmpty()) {
        setBackupResult(tr("tar non disponibile."), QString(), QStringLiteral("error"));
        return false;
    }

    auto *process = new QProcess(this);
    const QPointer<QProcess> guarded(process);
    m_backupProcess = process;
    m_backupCancelled = false;
    process->setProcessChannelMode(QProcess::SeparateChannels);
    process->setStandardOutputFile(QProcess::nullDevice());
    setBackupBusy(true);
    setBackupResult(tr("Verifica archivio in corso…"), canonical, QStringLiteral("running"));

    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this, guarded, canonical](int exitCode, QProcess::ExitStatus status) {
        if (!guarded || guarded != m_backupProcess)
            return;
        const QString details = QString::fromUtf8(guarded->readAllStandardError()).trimmed();
        const bool cancelled = m_backupCancelled;
        m_backupProcess = nullptr;
        guarded->deleteLater();
        setBackupBusy(false);
        m_backupCancelled = false;

        if (cancelled) {
            setBackupResult(tr("Verifica annullata."), canonical, QStringLiteral("cancelled"));
            return;
        }
        if (status == QProcess::NormalExit && exitCode == 0) {
            setBackupResult(tr("Archivio verificato correttamente."), canonical, QStringLiteral("success"));
            OperationLog::append(QStringLiteral("Backup"), QStringLiteral("verify"),
                                 QStringLiteral("success"), QFileInfo(canonical).fileName());
            return;
        }
        setBackupResult(details.isEmpty() ? tr("Archivio non valido o danneggiato.") : details,
                        canonical, QStringLiteral("error"));
        OperationLog::append(QStringLiteral("Backup"), QStringLiteral("verify"),
                             QStringLiteral("error"), QFileInfo(canonical).fileName());
    });

    connect(process, &QProcess::errorOccurred, this,
            [this, guarded, canonical](QProcess::ProcessError error) {
        if (!guarded || guarded != m_backupProcess || error != QProcess::FailedToStart)
            return;
        const QString message = guarded->errorString();
        m_backupProcess = nullptr;
        guarded->deleteLater();
        m_backupCancelled = false;
        setBackupBusy(false);
        setBackupResult(tr("Impossibile avviare la verifica: %1").arg(message),
                        canonical, QStringLiteral("error"));
        OperationLog::append(QStringLiteral("Backup"), QStringLiteral("verify"),
                             QStringLiteral("error"), QFileInfo(canonical).fileName());
    });

    process->start(tar, {QStringLiteral("-tzf"), canonical});
    return true;
}

bool SystemBackend::restoreSnapshot(const QString &path)
{
    if (m_backupBusy)
        return false;

    QString canonical;
    if (!validateBackupPath(path, &canonical)) {
        setBackupResult(tr("Archivio di backup non valido o fuori dalla cartella krisCC Backups."),
                        QString(), QStringLiteral("error"));
        return false;
    }

    const QString tar = resolveExecutable(QStringLiteral("tar"));
    if (tar.isEmpty()) {
        setBackupResult(tr("tar non disponibile."), QString(), QStringLiteral("error"));
        return false;
    }

    auto *process = new QProcess(this);
    const QPointer<QProcess> guarded(process);
    m_backupProcess = process;
    m_backupCancelled = false;
    process->setProcessChannelMode(QProcess::MergedChannels);
    setBackupBusy(true);
    setBackupResult(tr("Ripristino in corso…"), canonical, QStringLiteral("running"));

    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this, guarded, canonical](int exitCode, QProcess::ExitStatus status) {
        if (!guarded || guarded != m_backupProcess)
            return;
        const QString details = QString::fromUtf8(guarded->readAllStandardOutput()).trimmed();
        const bool cancelled = m_backupCancelled;
        m_backupProcess = nullptr;
        guarded->deleteLater();
        setBackupBusy(false);
        m_backupCancelled = false;

        if (cancelled) {
            setBackupResult(tr("Ripristino annullato. Alcuni file potrebbero essere già stati ripristinati."),
                            canonical, QStringLiteral("warning"));
            OperationLog::append(QStringLiteral("Backup"), QStringLiteral("restore"),
                                 QStringLiteral("cancelled"), QFileInfo(canonical).fileName());
            return;
        }
        if (status == QProcess::NormalExit && exitCode == 0) {
            setBackupResult(tr("Backup ripristinato. Disconnettersi o riavviare le applicazioni interessate per applicare tutte le configurazioni."),
                            canonical, QStringLiteral("success"));
            OperationLog::append(QStringLiteral("Backup"), QStringLiteral("restore"),
                                 QStringLiteral("success"), QFileInfo(canonical).fileName());
            notify(tr("Ripristino completato"), QFileInfo(canonical).fileName());
            return;
        }
        setBackupResult(details.isEmpty() ? tr("Ripristino non riuscito.") : details,
                        canonical, QStringLiteral("error"));
        OperationLog::append(QStringLiteral("Backup"), QStringLiteral("restore"),
                             QStringLiteral("error"), QFileInfo(canonical).fileName());
    });

    connect(process, &QProcess::errorOccurred, this,
            [this, guarded, canonical](QProcess::ProcessError error) {
        if (!guarded || guarded != m_backupProcess || error != QProcess::FailedToStart)
            return;
        const QString message = guarded->errorString();
        m_backupProcess = nullptr;
        guarded->deleteLater();
        m_backupCancelled = false;
        setBackupBusy(false);
        setBackupResult(tr("Impossibile avviare il ripristino: %1").arg(message),
                        canonical, QStringLiteral("error"));
        OperationLog::append(QStringLiteral("Backup"), QStringLiteral("restore"),
                             QStringLiteral("error"), QFileInfo(canonical).fileName());
    });

    process->start(tar, {QStringLiteral("-xzf"), canonical,
                         QStringLiteral("--no-same-owner"), QStringLiteral("--no-same-permissions"),
                         QStringLiteral("-C"), QDir::homePath()});
    return true;
}

QString SystemBackend::operationHistory() const
{
    return OperationLog::recent(50);
}

QVariantList SystemBackend::operationHistoryEntries() const
{
    QVariantList result;
    QFile file(QDir::homePath() + QStringLiteral("/.local/state/krisCC/history.jsonl"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return result;

    QList<QByteArray> lines;
    while (!file.atEnd()) {
        const QByteArray line = file.readLine().trimmed();
        if (!line.isEmpty())
            lines.append(line);
    }

    int emitted = 0;
    for (qsizetype i = lines.size(); i > 0 && emitted < 50; --i) {
        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(lines.at(i - 1), &error);
        if (error.error != QJsonParseError::NoError || !document.isObject())
            continue;
        const QJsonObject object = document.object();
        QVariantMap entry;
        const QString rawTime = object.value(QStringLiteral("time")).toString();
        const QDateTime parsedTime = QDateTime::fromString(rawTime, Qt::ISODate);
        entry.insert(QStringLiteral("time"),
                     parsedTime.isValid() ? QLocale().toString(parsedTime, QLocale::ShortFormat)
                                          : rawTime);
        entry.insert(QStringLiteral("category"), object.value(QStringLiteral("category")).toString());
        entry.insert(QStringLiteral("action"), object.value(QStringLiteral("action")).toString());
        entry.insert(QStringLiteral("state"), object.value(QStringLiteral("state")).toString());
        entry.insert(QStringLiteral("detail"), object.value(QStringLiteral("detail")).toString());
        result.append(entry);
        ++emitted;
    }
    return result;
}

bool SystemBackend::clearOperationHistory()
{
    return OperationLog::clear();
}

bool SystemBackend::createSnapshot(const QString &kind)
{
    if (m_backupBusy)
        return false;

    const QString tar = resolveExecutable(QStringLiteral("tar"));
    if (tar.isEmpty()) {
        setBackupResult(tr("tar non disponibile."), QString(), QStringLiteral("error"));
        return false;
    }

    const QString home = QDir::homePath();
    QDir backupDir(home + QStringLiteral("/krisCC Backups"));
    if (!backupDir.exists() && !backupDir.mkpath(QStringLiteral("."))) {
        setBackupResult(tr("Impossibile creare la cartella dei backup."), QString(), QStringLiteral("error"));
        return false;
    }

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

    if (kind != QStringLiteral("home") && kind != QStringLiteral("config")) {
        setBackupResult(tr("Tipo di snapshot non consentito."), QString(), QStringLiteral("error"));
        return false;
    }

    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    const QString label = kind == QStringLiteral("home") ? QStringLiteral("home") : QStringLiteral("config");
    const QString output = backupDir.filePath(QStringLiteral("%1-%2.tar.gz").arg(label, stamp));
    const QString partial = output + QStringLiteral(".partial");
    QFile::remove(partial);

    QStringList args = {QStringLiteral("-czf"), partial};
    if (kind == QStringLiteral("home")) {
        for (const QString &excluded : backupHomeExcludes())
            args << QStringLiteral("--exclude=./") + excluded;
        args << QStringLiteral("-C") << home << QStringLiteral(".");
    } else {
        QStringList entries;
        for (const QString &candidate : backupConfigEntries()) {
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

    auto *process = new QProcess(this);
    const QPointer<QProcess> guarded(process);
    m_backupProcess = process;
    m_backupPartialPath = partial;
    m_backupCancelled = false;
    process->setProcessChannelMode(QProcess::MergedChannels);
    setBackupBusy(true);
    setBackupResult(tr("Creazione snapshot in corso…"), output, QStringLiteral("running"));

    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this, guarded, output, partial](int exitCode, QProcess::ExitStatus status) {
        if (!guarded || guarded != m_backupProcess)
            return;
        const QString details = QString::fromUtf8(guarded->readAllStandardOutput()).trimmed();
        const bool cancelled = m_backupCancelled;
        m_backupProcess = nullptr;
        guarded->deleteLater();
        setBackupBusy(false);

        if (cancelled) {
            QFile::remove(partial);
            m_backupPartialPath.clear();
            m_backupCancelled = false;
            setBackupResult(tr("Backup annullato; il file parziale è stato rimosso."), QString(), QStringLiteral("cancelled"));
            OperationLog::append(QStringLiteral("Backup"), QStringLiteral("create"),
                                 QStringLiteral("cancelled"), QFileInfo(output).fileName());
            return;
        }

        const bool archiveProduced = QFileInfo(partial).exists() && QFileInfo(partial).size() > 0;
        const bool completed = status == QProcess::NormalExit && (exitCode == 0 || exitCode == 1) && archiveProduced;
        if (completed) {
            QFile::remove(output);
            if (!QFile::rename(partial, output)) {
                m_backupPartialPath = partial;
                setBackupResult(tr("Snapshot prodotto ma non è stato possibile finalizzarne il nome. Il file parziale è stato conservato."),
                                partial, QStringLiteral("error"));
                return;
            }
            m_backupPartialPath.clear();
            if (exitCode == 0) {
                setBackupResult(tr("Snapshot creato correttamente."), output, QStringLiteral("success"));
                OperationLog::append(QStringLiteral("Backup"), QStringLiteral("create"),
                                     QStringLiteral("success"), QFileInfo(output).fileName());
                notify(tr("Backup completato"), output);
            } else {
                const QString warning = details.isEmpty()
                    ? tr("Snapshot creato con avvisi da tar. Verificare l'archivio prima di usarlo per un ripristino.")
                    : tr("Snapshot creato con avvisi da tar. Verificare l'archivio prima di usarlo per un ripristino.\n%1").arg(details);
                setBackupResult(warning, output, QStringLiteral("warning"));
                OperationLog::append(QStringLiteral("Backup"), QStringLiteral("create"),
                                     QStringLiteral("warning"), QFileInfo(output).fileName());
                notify(tr("Backup completato con avvisi"), output);
            }
            return;
        }

        QFile::remove(partial);
        m_backupPartialPath.clear();
        const QString message = details.isEmpty()
            ? tr("Snapshot non riuscito (codice %1).").arg(exitCode)
            : details;
        setBackupResult(message, QString(), QStringLiteral("error"));
        OperationLog::append(QStringLiteral("Backup"), QStringLiteral("create"),
                             QStringLiteral("error"), QFileInfo(output).fileName());
    });

    connect(process, &QProcess::errorOccurred, this,
            [this, guarded, partial](QProcess::ProcessError error) {
        if (!guarded || guarded != m_backupProcess || error != QProcess::FailedToStart)
            return;
        const QString message = guarded->errorString();
        m_backupProcess = nullptr;
        guarded->deleteLater();
        QFile::remove(partial);
        m_backupPartialPath.clear();
        m_backupCancelled = false;
        setBackupBusy(false);
        setBackupResult(tr("Impossibile avviare il backup: %1").arg(message), QString(), QStringLiteral("error"));
    });

    process->start(tar, args);
    return true;
}

bool SystemBackend::cancelSnapshot()
{
    if (!m_backupProcess || !m_backupBusy)
        return false;
    m_backupCancelled = true;
    m_backupProcess->terminate();
    const QPointer<QProcess> guarded = m_backupProcess;
    QTimer::singleShot(2000, guarded, [guarded] {
        if (guarded && guarded->state() != QProcess::NotRunning)
            guarded->kill();
    });
    return true;
}

bool SystemBackend::openBackupFolder() const
{
    const QString path = QDir::homePath() + QStringLiteral("/krisCC Backups");
    QDir().mkpath(path);
    return QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

QVariantList SystemBackend::backupPreview(const QString &kind) const
{
    QVariantList result;
    const QString home = QDir::homePath();

    if (kind == QStringLiteral("config")) {
        for (const QString &entry : backupConfigEntries()) {
            QVariantMap item;
            item.insert(QStringLiteral("path"), QStringLiteral("~/") + entry);
            item.insert(QStringLiteral("included"), true);
            item.insert(QStringLiteral("exists"), QFileInfo::exists(home + QLatin1Char('/') + entry));
            result.append(item);
        }
        return result;
    }

    if (kind == QStringLiteral("home")) {
        QVariantMap allHome;
        allHome.insert(QStringLiteral("path"), QStringLiteral("~/"));
        allHome.insert(QStringLiteral("included"), true);
        allHome.insert(QStringLiteral("exists"), true);
        result.append(allHome);
        for (const QString &entry : backupHomeExcludes()) {
            QVariantMap item;
            item.insert(QStringLiteral("path"), QStringLiteral("~/") + entry);
            item.insert(QStringLiteral("included"), false);
            item.insert(QStringLiteral("exists"), QFileInfo::exists(home + QLatin1Char('/') + entry));
            result.append(item);
        }
    }
    return result;
}

void SystemBackend::setBackupBusy(bool busy)
{
    if (m_backupBusy == busy)
        return;
    m_backupBusy = busy;
    emit backupBusyChanged();
}

void SystemBackend::setBackupResult(const QString &status, const QString &path, const QString &state)
{
    m_backupStatus = status;
    m_backupPath = path;
    m_backupState = state;
    emit backupStatusChanged();
}

QString SystemBackend::readOsName() const
{
    QFile file(QStringLiteral("/etc/os-release"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return QSysInfo::prettyProductName();

    while (!file.atEnd()) {
        QString line = QString::fromUtf8(file.readLine()).trimmed();
        if (!line.startsWith(QStringLiteral("PRETTY_NAME=")))
            continue;
        QString value = line.mid(QStringLiteral("PRETTY_NAME=").size());
        if (value.size() >= 2 && value.startsWith('"') && value.endsWith('"'))
            value = value.mid(1, value.size() - 2);
        return value;
    }
    return QSysInfo::prettyProductName();
}
