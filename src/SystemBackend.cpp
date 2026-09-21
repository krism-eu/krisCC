#include "SystemBackend.h"

#include "OperationLog.h"
#include "PolkitHelper.h"
#include "Validators.h"
#include "ContractParsers.h"

#include <QClipboard>
#include <QCoreApplication>
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

#include <algorithm>

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
        QStringLiteral("bluetooth.service"),
        QStringLiteral("firewalld.service")
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
        QStringLiteral(".var/app/*/cache"),
        QStringLiteral("krisCC Backups"),
        QStringLiteral("KCC Backups"),
        QStringLiteral("K-ControlC Backups")
    };
    return entries;
}
}

SystemBackend::SystemBackend(PolkitHelper *polkit, QObject *parent)
    : QObject(parent)
    , m_polkit(polkit)
{
    if (m_polkit) {
        connect(m_polkit, &PolkitHelper::runningChanged, this, &SystemBackend::bootSelectionStateChanged);
        connect(m_polkit, &PolkitHelper::finished, this,
                [this](bool success, const QString &output) {
            if (!m_bootSelectionOwned)
                return;
            const QString kind = m_bootSelectionKind;
            m_bootSelectionOwned = false;
            m_bootSelectionRunning = false;
            m_bootSelectionKind.clear();
            m_bootSelectionState = success ? QStringLiteral("success") : QStringLiteral("error");
            emit bootSelectionStateChanged();
            emit bootSelectionFinished(kind, success, output);
            if (kind == QStringLiteral("uefi"))
                refreshUefiEntries();
            else if (kind == QStringLiteral("grub"))
                refreshGrubEntries();
        });
    }

    m_resourceTimer = new QTimer(this);
    m_resourceTimer->setInterval(2000);
    connect(m_resourceTimer, &QTimer::timeout, this, &SystemBackend::refreshResources);
}

bool SystemBackend::canSelectNextBoot() const
{
    return m_polkit && !m_polkit->running() && !m_bootSelectionRunning;
}

bool SystemBackend::uefiBootAvailable() const
{
    return !resolveExecutable(QStringLiteral("efibootmgr")).isEmpty();
}

bool SystemBackend::grubEntriesAvailable() const
{
    return !resolveExecutable(QStringLiteral("grubby")).isEmpty();
}

bool SystemBackend::grubNextBootAvailable() const
{
    return !resolveExecutable(QStringLiteral("grub2-reboot")).isEmpty();
}

void SystemBackend::refreshUefiEntries()
{
    if (m_bootEntriesBusy)
        return;

    const QString program = resolveExecutable(QStringLiteral("efibootmgr"));
    if (program.isEmpty()) {
        m_uefiEntries.clear();
        m_bootEntriesError = tr("efibootmgr non disponibile.");
        emit bootEntriesChanged();
        return;
    }

    m_bootEntriesBusy = true;
    m_bootEntriesError.clear();
    emit bootEntriesChanged();

    auto *process = new QProcess(this);
    const QPointer<QProcess> guard(process);
    m_bootEntriesProcess = process;
    process->setProcessChannelMode(QProcess::MergedChannels);

    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this, guard](int exitCode, QProcess::ExitStatus status) {
        if (!guard || guard != m_bootEntriesProcess)
            return;
        const bool timedOut = guard->property("krisccTimedOut").toBool();
        const QString output = QString::fromUtf8(guard->readAllStandardOutput());
        m_bootEntriesProcess = nullptr;
        guard->deleteLater();
        m_bootEntriesBusy = false;

        if (timedOut || status != QProcess::NormalExit || exitCode != 0) {
            m_uefiEntries.clear();
            m_bootEntriesError = timedOut ? tr("Tempo massimo superato leggendo le voci UEFI.")
                                          : tr("Impossibile leggere le voci UEFI.");
            emit bootEntriesChanged();
            return;
        }

        const auto parsed = ContractParsers::parseUefiEntries(output.toUtf8());
        m_uefiEntries = parsed.values;
        m_bootEntriesError.clear();
        emit bootEntriesChanged();
    });

    connect(process, &QProcess::errorOccurred, this,
            [this, guard](QProcess::ProcessError error) {
        if (!guard || guard != m_bootEntriesProcess || error != QProcess::FailedToStart)
            return;
        m_bootEntriesProcess = nullptr;
        guard->deleteLater();
        m_bootEntriesBusy = false;
        m_uefiEntries.clear();
        m_bootEntriesError = tr("Impossibile avviare efibootmgr.");
        emit bootEntriesChanged();
    });

    process->start(program, {});
    QTimer::singleShot(15000, process, [this, guard] {
        if (!guard || guard != m_bootEntriesProcess || guard->state() == QProcess::NotRunning)
            return;
        guard->setProperty("krisccTimedOut", true);
        guard->terminate();
        QTimer::singleShot(2000, guard, [guard] {
            if (guard && guard->state() != QProcess::NotRunning)
                guard->kill();
        });
    });
}

void SystemBackend::refreshGrubEntries()
{
    if (m_bootEntriesBusy)
        return;

    const QString program = resolveExecutable(QStringLiteral("grubby"));
    if (program.isEmpty()) {
        m_grubEntries.clear();
        m_bootEntriesError = tr("grubby non disponibile.");
        emit bootEntriesChanged();
        return;
    }

    m_bootEntriesBusy = true;
    m_bootEntriesError.clear();
    emit bootEntriesChanged();

    auto *process = new QProcess(this);
    const QPointer<QProcess> guard(process);
    m_bootEntriesProcess = process;
    process->setProcessChannelMode(QProcess::MergedChannels);

    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this, guard](int exitCode, QProcess::ExitStatus status) {
        if (!guard || guard != m_bootEntriesProcess)
            return;
        const bool timedOut = guard->property("krisccTimedOut").toBool();
        const QString output = QString::fromUtf8(guard->readAllStandardOutput());
        m_bootEntriesProcess = nullptr;
        guard->deleteLater();
        m_bootEntriesBusy = false;

        if (timedOut || status != QProcess::NormalExit || exitCode != 0) {
            m_grubEntries.clear();
            m_bootEntriesError = timedOut ? tr("Tempo massimo superato leggendo le voci GRUB/BLS.")
                                          : tr("Impossibile leggere le voci GRUB/BLS.");
            emit bootEntriesChanged();
            return;
        }

        const auto parsed = ContractParsers::parseGrubbyEntries(output.toUtf8());
        m_grubEntries = parsed.values;
        m_bootEntriesError.clear();
        emit bootEntriesChanged();
    });

    connect(process, &QProcess::errorOccurred, this,
            [this, guard](QProcess::ProcessError error) {
        if (!guard || guard != m_bootEntriesProcess || error != QProcess::FailedToStart)
            return;
        m_bootEntriesProcess = nullptr;
        guard->deleteLater();
        m_bootEntriesBusy = false;
        m_grubEntries.clear();
        m_bootEntriesError = tr("Impossibile avviare grubby.");
        emit bootEntriesChanged();
    });

    process->start(program, {QStringLiteral("--info=ALL")});
    QTimer::singleShot(15000, process, [this, guard] {
        if (!guard || guard != m_bootEntriesProcess || guard->state() == QProcess::NotRunning)
            return;
        guard->setProperty("krisccTimedOut", true);
        guard->terminate();
        QTimer::singleShot(2000, guard, [guard] {
            if (guard && guard->state() != QProcess::NotRunning)
                guard->kill();
        });
    });
}

bool SystemBackend::selectNextUefi(const QString &value)
{
    if (!canSelectNextBoot())
        return false;
    const QString token = value.trimmed();
    if (!Validators::bootToken(token))
        return false;

    m_bootSelectionOwned = true;
    m_bootSelectionRunning = true;
    m_bootSelectionKind = QStringLiteral("uefi");
    m_bootSelectionState = QStringLiteral("running");
    emit bootSelectionStateChanged();
    m_polkit->execute(QStringLiteral("/usr/libexec/kriscc/admin"),
                      {QStringLiteral("boot-next-uefi"), token});
    return true;
}

bool SystemBackend::selectNextGrub(const QString &value)
{
    if (!canSelectNextBoot())
        return false;
    const QString entry = value.trimmed();
    if (!Validators::grubEntry(entry))
        return false;

    m_bootSelectionOwned = true;
    m_bootSelectionRunning = true;
    m_bootSelectionKind = QStringLiteral("grub");
    m_bootSelectionState = QStringLiteral("running");
    emit bootSelectionStateChanged();
    m_polkit->execute(QStringLiteral("/usr/libexec/kriscc/admin"),
                      {QStringLiteral("boot-next-grub"), entry});
    return true;
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

    for (const QByteArray &rawLine : file.readAll().split('\n')) {
        const QByteArray line = rawLine.simplified();
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

void SystemBackend::refreshDashboardState()
{
    emit storageSummaryChanged();
    refreshServiceStates();
    refreshTopMemoryProcesses();
}

void SystemBackend::refreshTopMemoryProcesses()
{
    struct ProcessMemory {
        QString name;
        qint64 rssKiB = 0;
    };

    QList<ProcessMemory> entries;
    const QDir proc(QStringLiteral("/proc"));
    const QStringList pids = proc.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &pid : pids) {
        bool numeric = false;
        pid.toLongLong(&numeric);
        if (!numeric)
            continue;

        QFile status(proc.filePath(pid + QStringLiteral("/status")));
        if (!status.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;

        QString name;
        qint64 rssKiB = 0;
        while (!status.atEnd()) {
            const QByteArray raw = status.readLine();
            if (raw.startsWith("Name:"))
                name = QString::fromUtf8(raw.mid(5)).trimmed();
            else if (raw.startsWith("VmRSS:")) {
                const QList<QByteArray> fields = raw.simplified().split(' ');
                if (fields.size() >= 2)
                    rssKiB = fields.at(1).toLongLong();
            }
        }
        if (!name.isEmpty() && rssKiB > 0)
            entries.append({name, rssKiB});
    }

    std::sort(entries.begin(), entries.end(), [](const ProcessMemory &a, const ProcessMemory &b) {
        return a.rssKiB > b.rssKiB;
    });

    QVariantList result;
    const qsizetype limit = std::min<qsizetype>(5, entries.size());
    for (qsizetype i = 0; i < limit; ++i) {
        QVariantMap row;
        row.insert(QStringLiteral("name"), entries.at(i).name);
        row.insert(QStringLiteral("memoryMiB"), qRound64(double(entries.at(i).rssKiB) / 1024.0));
        result.append(row);
    }
    m_topMemoryProcesses = result;
    emit topMemoryProcessesChanged();
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
    out << tr("krisCC: ") << QCoreApplication::applicationVersion() << '\n';
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

bool SystemBackend::launchFlatpak(const QString &appId) const
{
    const QString id = appId.trimmed();
    const QString flatpak = resolveExecutable(QStringLiteral("flatpak"));
    if (flatpak.isEmpty() || !Validators::flatpakId(id))
        return false;
    return QProcess::startDetached(flatpak, {QStringLiteral("run"), id});
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

void SystemBackend::refreshServiceStates()
{
    const quint64 generation = ++m_serviceRefreshGeneration;
    for (const QString &service : allowedServices())
        m_serviceStates.insert(service, tr("lettura…"));
    emit serviceStatesChanged();

    for (const QString &service : allowedServices()) {
        QDBusInterface manager(QStringLiteral("org.freedesktop.systemd1"),
                               QStringLiteral("/org/freedesktop/systemd1"),
                               QStringLiteral("org.freedesktop.systemd1.Manager"),
                               QDBusConnection::systemBus());
        if (!manager.isValid()) {
            if (generation == m_serviceRefreshGeneration) {
                m_serviceStates.insert(service, tr("non disponibile"));
                emit serviceStatesChanged();
            }
            continue;
        }

        auto *unitWatcher = new QDBusPendingCallWatcher(
            manager.asyncCall(QStringLiteral("GetUnit"), service), this);
        connect(unitWatcher, &QDBusPendingCallWatcher::finished, this,
                [this, service, generation](QDBusPendingCallWatcher *call) {
            const QDBusPendingReply<QDBusObjectPath> unitReply(*call);
            call->deleteLater();
            if (generation != m_serviceRefreshGeneration)
                return;

            if (unitReply.isError()) {
                m_serviceStates.insert(service, tr("non disponibile"));
                emit serviceStatesChanged();
                return;
            }

            QDBusInterface properties(QStringLiteral("org.freedesktop.systemd1"),
                                      unitReply.value().path(),
                                      QStringLiteral("org.freedesktop.DBus.Properties"),
                                      QDBusConnection::systemBus());
            if (!properties.isValid()) {
                m_serviceStates.insert(service, tr("sconosciuto"));
                emit serviceStatesChanged();
                return;
            }

            auto *stateWatcher = new QDBusPendingCallWatcher(
                properties.asyncCall(QStringLiteral("Get"),
                                     QStringLiteral("org.freedesktop.systemd1.Unit"),
                                     QStringLiteral("ActiveState")),
                this);
            connect(stateWatcher, &QDBusPendingCallWatcher::finished, this,
                    [this, service, generation](QDBusPendingCallWatcher *stateCall) {
                const QDBusPendingReply<QDBusVariant> stateReply(*stateCall);
                stateCall->deleteLater();
                if (generation != m_serviceRefreshGeneration)
                    return;

                m_serviceStates.insert(
                    service,
                    stateReply.isError()
                        ? tr("sconosciuto")
                        : stateReply.value().variant().toString());
                emit serviceStatesChanged();
            });
        });
    }
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
    manager.setInteractiveAuthorizationAllowed(true);

    auto *watcher = new QDBusPendingCallWatcher(
        manager.asyncCall(QStringLiteral("RestartUnit"), service, QStringLiteral("replace")), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, service](QDBusPendingCallWatcher *call) {
        const QDBusPendingReply<QDBusObjectPath> reply(*call);
        notify(reply.isError() ? tr("Riavvio servizio non riuscito") : tr("Servizio riavviato"),
               reply.isError() ? reply.error().message() : service);
        call->deleteLater();
        QTimer::singleShot(400, this, &SystemBackend::refreshServiceStates);
    });
    return true;
}

void SystemBackend::requestReboot()
{
    QDBusInterface manager(QStringLiteral("org.freedesktop.login1"),
                           QStringLiteral("/org/freedesktop/login1"),
                           QStringLiteral("org.freedesktop.login1.Manager"),
                           QDBusConnection::systemBus());
    manager.setInteractiveAuthorizationAllowed(true);
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
            QFile::setPermissions(output, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
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

bool SystemBackend::deleteSnapshot(const QString &path)
{
    if (m_backupBusy)
        return false;
    QString canonical;
    if (!validateBackupPath(path, &canonical))
        return false;
    const QString name = QFileInfo(canonical).fileName();
    if (!QFile::remove(canonical))
        return false;
    OperationLog::append(QStringLiteral("Backup"), QStringLiteral("delete"),
                         QStringLiteral("success"), name);