#include "SystemBackend.h"
#include "Validators.h"

#include "OperationLog.h"
#include "PolkitHelper.h"
#include "ProcessRunner.h"
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
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QHostAddress>
#include <QLocale>
#include <QNetworkAccessManager>
#include <QNetworkAddressEntry>
#include <QNetworkInterface>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QHash>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QSettings>
#include <QStorageInfo>
#include <QSysInfo>
#include <QTextStream>
#include <QTimeZone>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>
#include <QVersionNumber>

#include <sys/sysinfo.h>
#include <unistd.h>

#include <algorithm>

namespace {
constexpr int kBackupVerifyTimeoutMs = 10 * 60 * 1000;
constexpr int kBackupOperationTimeoutMs = 30 * 60 * 1000;

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
        QStringLiteral("firewalld.service"),
        QStringLiteral("wpa_supplicant.service"),
        QStringLiteral("iwd.service")
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
            if (m_adminMaintenanceOwned) {
                const QString operation = m_adminMaintenanceOperation;
                m_adminMaintenanceOwned = false;
                m_adminMaintenanceOperation.clear();
                emit adminMaintenanceFinished(operation, success, output);
                // Notification ownership stays in main.cpp for Polkit operations.
                return;
            }
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

    QSettings settings;
    const QString configuredBackupDirectory =
        settings.value(QStringLiteral("backup/directory"), defaultBackupDirectory()).toString();
    QString canonicalBackupDirectory;
    if (validateBackupDirectory(configuredBackupDirectory, &canonicalBackupDirectory))
        m_backupDirectory = canonicalBackupDirectory;
    else
        m_backupDirectory = defaultBackupDirectory();

    m_networkAccess = new QNetworkAccessManager(this);

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
        m_nextUefiBootLabel.clear();
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
            m_nextUefiBootLabel.clear();
            m_bootEntriesError = timedOut ? tr("Tempo massimo superato leggendo le voci UEFI.")
                                          : tr("Impossibile leggere le voci UEFI.");
            emit bootEntriesChanged();
            return;
        }

        const auto parsed = ContractParsers::parseUefiEntries(output.toUtf8());
        m_uefiEntries = parsed.values;
        m_nextUefiBootLabel.clear();

        QString preferredCode;
        const QRegularExpression bootNextPattern(QStringLiteral("(?m)^BootNext:\\s*([0-9A-Fa-f]{4})\\s*$"));
        const QRegularExpression bootOrderPattern(QStringLiteral("(?m)^BootOrder:\\s*([0-9A-Fa-f]{4})"));
        QRegularExpressionMatch match = bootNextPattern.match(output);
        if (match.hasMatch())
            preferredCode = match.captured(1).toUpper();
        else {
            match = bootOrderPattern.match(output);
            if (match.hasMatch())
                preferredCode = match.captured(1).toUpper();
        }
        for (const QVariant &value : m_uefiEntries) {
            const QVariantMap row = value.toMap();
            if (row.value(QStringLiteral("code")).toString() == preferredCode) {
                m_nextUefiBootLabel = row.value(QStringLiteral("label")).toString();
                break;
            }
        }
        if (m_nextUefiBootLabel.isEmpty() && !preferredCode.isEmpty())
            m_nextUefiBootLabel = preferredCode;
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
        m_nextUefiBootLabel.clear();
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
    if (m_internetIdentityReply && m_internetIdentityReply->isRunning())
        m_internetIdentityReply->abort();
    if (m_backupRunner && m_backupRunner->running())
        m_backupRunner->cancel();
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
    const auto summaryFor = [](const QString &path) -> QString {
        QStorageInfo storage(path);
        if (!storage.isValid() || !storage.isReady() || storage.bytesTotal() == 0)
            return QString();
        return QCoreApplication::translate("SystemBackend", "%1 liberi su %2")
            .arg(humanGiB(storage.bytesAvailable()), humanGiB(storage.bytesTotal()));
    };

    QString home = summaryFor(QDir::homePath());
    if (home.isEmpty())
        home = summaryFor(QStringLiteral("/var/home"));

    QString system = summaryFor(QStringLiteral("/sysroot"));
    if (system.isEmpty())
        system = summaryFor(QStringLiteral("/"));

    if (home.isEmpty() && system.isEmpty())
        return tr("Non disponibile");

    QStringList lines;
    if (!home.isEmpty())
        lines.append(tr("Home: %1").arg(home));
    if (!system.isEmpty())
        lines.append(tr("Sistema: %1").arg(system));
    return lines.join(QLatin1Char('\n'));
}

void SystemBackend::refreshDashboardState()
{
    emit storageSummaryChanged();
    refreshServiceStates();
    refreshTopMemoryProcesses();
    refreshNetworkState();
    if (uefiBootAvailable())
        refreshUefiEntries();
}

void SystemBackend::refreshTopMemoryProcesses()
{
    QHash<QString, qint64> aggregatedMemory;
    QHash<QString, int> processCounts;

    const QDir proc(QStringLiteral("/proc"));
    const QStringList pids = proc.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &pid : pids) {
        bool numeric = false;
        const qlonglong parsedPid = pid.toLongLong(&numeric);
        if (!numeric || parsedPid <= 0)
            continue;

        QString name;
        qint64 rssKiB = 0;

        QFile status(proc.filePath(pid + QStringLiteral("/status")));
        if (status.open(QIODevice::ReadOnly | QIODevice::Text)) {
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
        }

        if (name.isEmpty()) {
            QFile comm(proc.filePath(pid + QStringLiteral("/comm")));
            if (comm.open(QIODevice::ReadOnly | QIODevice::Text))
                name = QString::fromUtf8(comm.readAll()).trimmed();
        }

        if (rssKiB <= 0) {
            QFile statm(proc.filePath(pid + QStringLiteral("/statm")));
            if (statm.open(QIODevice::ReadOnly | QIODevice::Text)) {
                const QList<QByteArray> fields = statm.readLine().simplified().split(' ');
                if (fields.size() >= 2) {
                    bool ok = false;
                    const qint64 residentPages = fields.at(1).toLongLong(&ok);
                    const long pageSize = ::sysconf(_SC_PAGESIZE);
                    if (ok && residentPages > 0 && pageSize > 0)
                        rssKiB = residentPages * qint64(pageSize) / 1024;
                }
            }
        }

        if (!name.isEmpty() && rssKiB > 0) {
            aggregatedMemory[name] += rssKiB;
            processCounts[name] += 1;
        }
    }

    struct ProcessMemory {
        QString displayName;
        qint64 rssKiB = 0;
    };

    QList<ProcessMemory> entries;
    entries.reserve(aggregatedMemory.size());
    for (auto it = aggregatedMemory.constBegin(); it != aggregatedMemory.constEnd(); ++it) {
        const QString &procName = it.key();
        const int count = processCounts.value(procName, 1);
        const QString displayName = (count > 1)
            ? QStringLiteral("%1 (%2 processi)").arg(procName).arg(count)
            : procName;
        entries.append({displayName, it.value()});
    }

    std::sort(entries.begin(), entries.end(), [](const ProcessMemory &a, const ProcessMemory &b) {
        return a.rssKiB > b.rssKiB;
    });

    QVariantList result;
    const qsizetype limit = std::min<qsizetype>(10, entries.size());
    for (qsizetype i = 0; i < limit; ++i) {
        QVariantMap row;
        row.insert(QStringLiteral("name"), entries.at(i).displayName);
        row.insert(QStringLiteral("memoryMiB"), qRound64(double(entries.at(i).rssKiB) / 1024.0));
        result.append(row);
    }
    m_topMemoryProcesses = result;
    emit topMemoryProcessesChanged();
}

void SystemBackend::refreshNetworkState()
{
    QString selectedInterface;
    QString selectedDisplayName;
    QString selectedAddress;
    QString selectedState = QStringLiteral("down");
    QString selectedKind = QStringLiteral("ethernet");
    int bestScore = -1;

    const QList<QNetworkInterface> interfaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface &iface : interfaces) {
        const auto flags = iface.flags();
        if (!flags.testFlag(QNetworkInterface::IsUp)
            || !flags.testFlag(QNetworkInterface::IsRunning)
            || flags.testFlag(QNetworkInterface::IsLoopBack)
            || iface.type() == QNetworkInterface::Virtual)
            continue;

        QString ipv4;
        QString ipv6;
        for (const QNetworkAddressEntry &entry : iface.addressEntries()) {
            const QHostAddress address = entry.ip();
            if (address.isNull() || address.isLoopback() || address.isLinkLocal())
                continue;
            if (address.protocol() == QAbstractSocket::IPv4Protocol && ipv4.isEmpty())
                ipv4 = address.toString();
            else if (address.protocol() == QAbstractSocket::IPv6Protocol && ipv6.isEmpty())
                ipv6 = address.toString();
        }

        int score = !ipv4.isEmpty() ? 100 : (!ipv6.isEmpty() ? 80 : 20);
        if (iface.type() == QNetworkInterface::Ethernet)
            score += 20;
        else if (iface.type() == QNetworkInterface::Wifi)
            score += 10;

        if (score <= bestScore)
            continue;
        bestScore = score;
        // Operational APIs such as nmcli require the kernel interface name.
        selectedInterface = iface.name();
        selectedDisplayName = iface.humanReadableName().isEmpty() ? iface.name() : iface.humanReadableName();
        selectedAddress = !ipv4.isEmpty() ? ipv4 : ipv6;
        selectedState = !ipv4.isEmpty() ? QStringLiteral("ipv4")
                      : !ipv6.isEmpty() ? QStringLiteral("ipv6")
                                        : QStringLiteral("up");
        selectedKind = iface.type() == QNetworkInterface::Wifi
            ? QStringLiteral("wifi") : QStringLiteral("ethernet");
    }

    if (selectedInterface == m_networkInterface
        && selectedDisplayName == m_networkDisplayName
        && selectedAddress == m_networkAddress
        && selectedState == m_networkState
        && selectedKind == m_networkKind)
        return;

    m_networkInterface = selectedInterface;
    m_networkDisplayName = selectedDisplayName;
    m_networkAddress = selectedAddress;
    m_networkState = selectedState;
    m_networkKind = selectedKind;
    emit networkChanged();
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
        {QStringLiteral("konsole"), QStringLiteral("konsole")},
        {QStringLiteral("kfind"), QStringLiteral("kfind")},
        {QStringLiteral("isoimagewriter"), QStringLiteral("isoimagewriter")}
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

bool SystemBackend::cockpitAvailable() const
{
    return !resolveExecutable(QStringLiteral("cockpit-ws")).isEmpty()
        || QFileInfo::exists(QStringLiteral("/usr/lib/systemd/system/cockpit.socket"))
        || QFileInfo::exists(QStringLiteral("/etc/systemd/system/cockpit.socket"));
}

bool SystemBackend::openWebConsole()
{
    if (!cockpitAvailable()) {
        notify(tr("Cockpit non disponibile"), tr("La console web non risulta installata."));
        return false;
    }

    QDBusInterface manager(QStringLiteral("org.freedesktop.systemd1"),
                           QStringLiteral("/org/freedesktop/systemd1"),
                           QStringLiteral("org.freedesktop.systemd1.Manager"),
                           QDBusConnection::systemBus());
    if (!manager.isValid()) {
        notify(tr("Cockpit non disponibile"), tr("systemd non è disponibile sul bus di sistema."));
        return false;
    }

    manager.setInteractiveAuthorizationAllowed(true);
    auto *watcher = new QDBusPendingCallWatcher(
        manager.asyncCall(QStringLiteral("StartUnit"),
                          QStringLiteral("cockpit.socket"),
                          QStringLiteral("replace")),
        this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this](QDBusPendingCallWatcher *call) {
        const QDBusPendingReply<QDBusObjectPath> reply(*call);
        call->deleteLater();
        if (reply.isError()) {
            notify(tr("Avvio Cockpit non riuscito"), reply.error().message());
            return;
        }
        if (!QDesktopServices::openUrl(QUrl(QStringLiteral("https://localhost:9090"))))
            notify(tr("Apertura Cockpit non riuscita"), tr("Impossibile aprire il browser predefinito."));
    });
    return true;
}

bool SystemBackend::vacuumJournal()
{
    if (!m_polkit || m_polkit->running() || m_adminMaintenanceOwned)
        return false;
    m_adminMaintenanceOwned = true;
    m_adminMaintenanceOperation = QStringLiteral("journal-vacuum");
    m_polkit->execute(QStringLiteral("/usr/libexec/kriscc/admin"), {QStringLiteral("journal-vacuum")});
    return true;
}

bool SystemBackend::cleanDnfCache()
{
    if (!m_polkit || m_polkit->running() || m_adminMaintenanceOwned)
        return false;
    m_adminMaintenanceOwned = true;
    m_adminMaintenanceOperation = QStringLiteral("dnf-clean");
    m_polkit->execute(QStringLiteral("/usr/libexec/kriscc/admin"), {QStringLiteral("dnf-clean")});
    return true;
}

void SystemBackend::checkInternetIdentity()
{
    if (m_internetIdentityBusy || !m_networkAccess)
        return;

    QStringList dnsServers;
    const QStringList resolverFiles = {
        QStringLiteral("/run/systemd/resolve/resolv.conf"),
        QStringLiteral("/etc/resolv.conf")
    };
    for (const QString &path : resolverFiles) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;
        while (!file.atEnd()) {
            const QString line = QString::fromUtf8(file.readLine()).trimmed();
            if (!line.startsWith(QStringLiteral("nameserver ")))
                continue;
            const QString address = line.section(QLatin1Char(' '), 1, 1).trimmed();
            if (!address.isEmpty() && !dnsServers.contains(address))
                dnsServers.append(address);
        }
        if (!dnsServers.isEmpty())
            break;
    }

    m_internetIdentityBusy = true;
    m_internetIdentity = tr("Verifica della connessione Internet in corso…");
    emit internetIdentityChanged();

    QNetworkRequest request(QUrl(QStringLiteral("https://api.ipify.org")));
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("krisCC/%1").arg(QCoreApplication::applicationVersion()));
    QNetworkReply *reply = m_networkAccess->get(request);
    m_internetIdentityReply = reply;
    QTimer::singleShot(10000, reply, [reply] {
        if (reply->isRunning())
            reply->abort();
    });

    connect(reply, &QNetworkReply::finished, this, [this, reply, dnsServers] {
        if (m_internetIdentityReply != reply) {
            reply->deleteLater();
            return;
        }
        m_internetIdentityReply = nullptr;
        m_internetIdentityBusy = false;

        QString publicAddress;
        if (reply->error() == QNetworkReply::NoError) {
            const QString candidate = QString::fromUtf8(reply->readAll()).trimmed();
            QHostAddress parsed(candidate);
            if (!parsed.isNull())
                publicAddress = candidate;
        }

        const QString dns = dnsServers.isEmpty() ? tr("non disponibile") : dnsServers.join(QStringLiteral(", "));
        m_internetIdentity = tr("IP pubblico: %1\nDNS in uso: %2")
                                 .arg(publicAddress.isEmpty() ? tr("non disponibile") : publicAddress,
                                      dns);
        reply->deleteLater();
        emit internetIdentityChanged();
    });
}

bool SystemBackend::openNetworkSettings() const
{
    const QString shell = resolveExecutable(QStringLiteral("kcmshell6"));
    if (!shell.isEmpty())
        return QProcess::startDetached(shell, {QStringLiteral("kcm_networkmanagement")});
    const QString settings = toolProgram(QStringLiteral("systemsettings"));
    return !settings.isEmpty() && QProcess::startDetached(settings, {});
}

QStringList SystemBackend::kernelArguments() const
{
    QFile file(QStringLiteral("/proc/cmdline"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    return QString::fromUtf8(file.readAll()).trimmed().split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
}

bool SystemBackend::openTemporaryFolder() const
{
    return QDesktopServices::openUrl(QUrl::fromLocalFile(QDir::tempPath()));
}

bool SystemBackend::openHomeFolder() const
{
    return QDesktopServices::openUrl(QUrl::fromLocalFile(QDir::homePath()));
}

bool SystemBackend::openRootFolder() const
{
    return QDesktopServices::openUrl(QUrl::fromLocalFile(QStringLiteral("/")));
}

bool SystemBackend::programAvailable(const QString &program) const
{
    return !resolveExecutable(program).isEmpty();
}

void SystemBackend::checkControlCenterUpdate()
{
    if (m_controlCenterUpdateBusy || !m_networkAccess)
        return;

    m_controlCenterUpdateBusy = true;
    m_controlCenterUpdateAvailable = false;
    m_controlCenterLatestVersion.clear();
    m_controlCenterUpdateStatus = tr("Verifica aggiornamenti in corso…");
    emit controlCenterUpdateChanged();

    QNetworkRequest request(
        QUrl(QStringLiteral("https://api.github.com/repos/krism-eu/krisCC/releases/latest")));
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setRawHeader("User-Agent",
                         QByteArray("krisCC/") + QCoreApplication::applicationVersion().toUtf8());

    QNetworkReply *reply = m_networkAccess->get(request);
    QTimer::singleShot(15000, reply, [reply] {
        if (reply->isRunning())
            reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        const auto finishError = [this](const QString &message) {
            m_controlCenterUpdateBusy = false;
            m_controlCenterUpdateAvailable = false;
            m_controlCenterLatestVersion.clear();
            m_controlCenterUpdateStatus = message;
            emit controlCenterUpdateChanged();
        };

        if (reply->error() != QNetworkReply::NoError) {
            const QString error = reply->errorString();
            reply->deleteLater();
            finishError(tr("Verifica aggiornamenti non riuscita: %1").arg(error));
            return;
        }

        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(reply->readAll(), &parseError);
        reply->deleteLater();
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            finishError(tr("Risposta release non valida."));
            return;
        }

        const QJsonObject release = document.object();
        if (release.value(QStringLiteral("draft")).toBool()
            || release.value(QStringLiteral("prerelease")).toBool()) {
            finishError(tr("La release stable restituita non è valida."));
            return;
        }

        const QString tag = release.value(QStringLiteral("tag_name")).toString().trimmed();
        static const QRegularExpression tagPattern(
            QStringLiteral("^v([0-9]+\\.[0-9]+\\.[0-9]+)$"));
        const QRegularExpressionMatch match = tagPattern.match(tag);
        if (!match.hasMatch()) {
            finishError(tr("Versione release non riconosciuta."));
            return;
        }

        const QString latest = match.captured(1);

        const QVersionNumber current =
            QVersionNumber::fromString(QCoreApplication::applicationVersion());
        const QVersionNumber remote = QVersionNumber::fromString(latest);
        if (current.isNull() || remote.isNull()) {
            finishError(tr("Impossibile confrontare le versioni del Control Center."));
            return;
        }

        m_controlCenterUpdateBusy = false;
        m_controlCenterLatestVersion = latest;
        const int comparison = QVersionNumber::compare(remote, current);
        m_controlCenterUpdateAvailable = comparison > 0;
        if (comparison > 0) {
            m_controlCenterUpdateStatus =
                tr("Disponibile krisCC %1 (installata %2). krisCC fa parte dell'immagine KrisOS: applica l'aggiornamento dalla sezione Sistema.")
                    .arg(latest, QCoreApplication::applicationVersion());
        } else if (comparison == 0) {
            m_controlCenterUpdateStatus =
                tr("Il Control Center è già aggiornato (%1).").arg(latest);
        } else {
            m_controlCenterUpdateStatus =
                tr("La versione installata %1 è più recente della stable %2.")
                    .arg(QCoreApplication::applicationVersion(), latest);
        }
        emit controlCenterUpdateChanged();
    });
}

void SystemBackend::refreshServiceStates()
{
    const quint64 generation = ++m_serviceRefreshGeneration;
    for (const QString &service : allowedServices())
        m_serviceStates.insert(service, QStringLiteral("loading"));
    emit serviceStatesChanged();

    for (const QString &service : allowedServices()) {
        QDBusInterface manager(QStringLiteral("org.freedesktop.systemd1"),
                               QStringLiteral("/org/freedesktop/systemd1"),
                               QStringLiteral("org.freedesktop.systemd1.Manager"),
                               QDBusConnection::systemBus());
        if (!manager.isValid()) {
            if (generation == m_serviceRefreshGeneration) {
                m_serviceStates.insert(service, QStringLiteral("missing"));
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
                m_serviceStates.insert(service, QStringLiteral("missing"));
                emit serviceStatesChanged();
                return;
            }

            QDBusInterface properties(QStringLiteral("org.freedesktop.systemd1"),
                                      unitReply.value().path(),
                                      QStringLiteral("org.freedesktop.DBus.Properties"),
                                      QDBusConnection::systemBus());
            if (!properties.isValid()) {
                m_serviceStates.insert(service, QStringLiteral("unknown"));
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
                        ? QStringLiteral("unknown")
                        : stateReply.value().variant().toString());
                emit serviceStatesChanged();
            });
        });
    }
}

bool SystemBackend::startService(const QString &service)
{
    if (!allowedServices().contains(service)) {
        notify(tr("Avvio servizio non riuscito"), tr("Servizio non autorizzato dal Control Center: %1").arg(service));
        return false;
    }
    QDBusInterface manager(QStringLiteral("org.freedesktop.systemd1"), QStringLiteral("/org/freedesktop/systemd1"),
                           QStringLiteral("org.freedesktop.systemd1.Manager"), QDBusConnection::systemBus());
    if (!manager.isValid()) {
        notify(tr("Avvio servizio non riuscito"), tr("systemd non è disponibile sul bus di sistema."));
        return false;
    }
    manager.setInteractiveAuthorizationAllowed(true);
    auto *watcher = new QDBusPendingCallWatcher(
        manager.asyncCall(QStringLiteral("StartUnit"), service, QStringLiteral("replace")), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, service](QDBusPendingCallWatcher *call) {
        const QDBusPendingReply<QDBusObjectPath> reply(*call);
        notify(reply.isError() ? tr("Avvio servizio non riuscito") : tr("Servizio avviato"),
               reply.isError() ? reply.error().message() : service);
        call->deleteLater();
        QTimer::singleShot(400, this, &SystemBackend::refreshServiceStates);
    });
    return true;
}

bool SystemBackend::stopService(const QString &service)
{
    if (!allowedServices().contains(service)) {
        notify(tr("Arresto servizio non riuscito"), tr("Servizio non autorizzato dal Control Center: %1").arg(service));
        return false;
    }
    QDBusInterface manager(QStringLiteral("org.freedesktop.systemd1"), QStringLiteral("/org/freedesktop/systemd1"),
                           QStringLiteral("org.freedesktop.systemd1.Manager"), QDBusConnection::systemBus());
    if (!manager.isValid()) {
        notify(tr("Arresto servizio non riuscito"), tr("systemd non è disponibile sul bus di sistema."));
        return false;
    }
    manager.setInteractiveAuthorizationAllowed(true);
    auto *watcher = new QDBusPendingCallWatcher(
        manager.asyncCall(QStringLiteral("StopUnit"), service, QStringLiteral("replace")), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, service](QDBusPendingCallWatcher *call) {
        const QDBusPendingReply<QDBusObjectPath> reply(*call);
        notify(reply.isError() ? tr("Arresto servizio non riuscito") : tr("Servizio arrestato"),
               reply.isError() ? reply.error().message() : service);
        call->deleteLater();
        QTimer::singleShot(400, this, &SystemBackend::refreshServiceStates);
    });
    return true;
}

bool SystemBackend::resetFailedService(const QString &service)
{
    if (!allowedServices().contains(service)) {
        notify(tr("Reset stato fallito non riuscito"), tr("Servizio non autorizzato dal Control Center: %1").arg(service));
        return false;
    }
    QDBusInterface manager(QStringLiteral("org.freedesktop.systemd1"),
                           QStringLiteral("/org/freedesktop/systemd1"),
                           QStringLiteral("org.freedesktop.systemd1.Manager"),
                           QDBusConnection::systemBus());
    if (!manager.isValid()) {
        notify(tr("Reset stato fallito non riuscito"), tr("systemd non è disponibile sul bus di sistema."));
        return false;
    }
    manager.setInteractiveAuthorizationAllowed(true);

    auto *watcher = new QDBusPendingCallWatcher(
        manager.asyncCall(QStringLiteral("ResetFailedUnit"), service), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, service](QDBusPendingCallWatcher *call) {
        const QDBusPendingReply<> reply(*call);
        notify(reply.isError() ? tr("Reset stato fallito non riuscito")
                               : tr("Stato fallito reimpostato"),
               reply.isError() ? reply.error().message() : service);
        call->deleteLater();
        QTimer::singleShot(400, this, &SystemBackend::refreshServiceStates);
    });
    return true;
}

bool SystemBackend::restartService(const QString &service)
{
    if (!allowedServices().contains(service)) {
        notify(tr("Riavvio servizio non riuscito"), tr("Servizio non autorizzato dal Control Center: %1").arg(service));
        return false;
    }

    QDBusInterface manager(QStringLiteral("org.freedesktop.systemd1"),
                           QStringLiteral("/org/freedesktop/systemd1"),
                           QStringLiteral("org.freedesktop.systemd1.Manager"),
                           QDBusConnection::systemBus());
    if (!manager.isValid()) {
        notify(tr("Riavvio servizio non riuscito"), tr("systemd non è disponibile sul bus di sistema."));
        return false;
    }
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

void SystemBackend::requestFirmwareReboot()
{
    QDBusInterface manager(QStringLiteral("org.freedesktop.login1"),
                           QStringLiteral("/org/freedesktop/login1"),
                           QStringLiteral("org.freedesktop.login1.Manager"),
                           QDBusConnection::systemBus());
    manager.setInteractiveAuthorizationAllowed(true);
    if (!manager.isValid()) {
        emit rebootFinished(false, tr("systemd-logind non disponibile."));
        return;
    }
    auto *watcher = new QDBusPendingCallWatcher(
        manager.asyncCall(QStringLiteral("SetRebootToFirmwareSetup"), true), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this](QDBusPendingCallWatcher *call) {
        const QDBusPendingReply<> reply(*call);
        call->deleteLater();
        if (reply.isError()) {
            emit rebootFinished(false, tr("Firmware setup non disponibile: %1").arg(reply.error().message()));
            return;
        }
        requestReboot();
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

QString SystemBackend::defaultBackupDirectory() const
{
    return QDir::home().filePath(QStringLiteral("krisCC Backups"));
}

bool SystemBackend::validateBackupDirectory(const QString &path, QString *canonicalPath) const
{
    QString localPath = path.trimmed();
    if (localPath.startsWith(QStringLiteral("file:")))
        localPath = QUrl(localPath).toLocalFile();
    if (localPath.isEmpty())
        return false;

    const QFileInfo info(localPath);
    const QString canonical = info.canonicalFilePath();
    if (canonical.isEmpty() || !info.isDir() || !info.isWritable())
        return false;
    if (canonicalPath)
        *canonicalPath = canonical;
    return true;
}

QString SystemBackend::currentBackupRoot() const
{
    QString canonical;
    return validateBackupDirectory(m_backupDirectory, &canonical) ? canonical : QString();
}

bool SystemBackend::setBackupDirectory(const QString &pathOrUrl)
{
    if (m_backupBusy)
        return false;

    QString localPath = pathOrUrl.trimmed();
    if (localPath.startsWith(QStringLiteral("file:")))
        localPath = QUrl(localPath).toLocalFile();
    if (localPath.isEmpty())
        return false;

    QDir directory(localPath);
    if (!directory.exists() && !directory.mkpath(QStringLiteral("."))) {
        setBackupResult(tr("La cartella backup non è disponibile o scrivibile."),
                        QString(), QStringLiteral("error"));
        return false;
    }

    QString canonical;
    if (!validateBackupDirectory(directory.absolutePath(), &canonical)) {
        setBackupResult(tr("La cartella backup non è disponibile o scrivibile."),
                        QString(), QStringLiteral("error"));
        return false;
    }
    if (m_backupDirectory == canonical)
        return true;

    m_backupDirectory = canonical;
    QSettings settings;
    settings.setValue(QStringLiteral("backup/directory"), m_backupDirectory);
    emit backupDirectoryChanged();
    return true;
}

bool SystemBackend::backupIsLocalSnapshot() const
{
    const QString root = currentBackupRoot();
    if (root.isEmpty())
        return true;
    const QStorageInfo backupStorage(root);
    const QStorageInfo homeStorage(QDir::homePath());
    if (!backupStorage.isValid() || !homeStorage.isValid())
        return true;
    return backupStorage.device() == homeStorage.device();
}

QVariantList SystemBackend::backups() const
{
    QVariantList result;
    const QString backupRoot = currentBackupRoot();
    if (backupRoot.isEmpty())
        return result;
    const QDir backupDir(backupRoot);
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
    const QString backupRoot = currentBackupRoot();
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
        setBackupResult(tr("Archivio di backup non valido o fuori dalla cartella backup selezionata."),
                        QString(), QStringLiteral("error"));
        return false;
    }

    const QString tar = resolveExecutable(QStringLiteral("tar"));
    if (tar.isEmpty()) {
        setBackupResult(tr("tar non disponibile."), QString(), QStringLiteral("error"));
        return false;
    }

    auto *runner = new ProcessRunner(this);
    m_backupRunner = runner;
    setBackupBusy(true);
    setBackupResult(tr("Verifica archivio in corso…"), canonical, QStringLiteral("running"));

    connect(runner, &ProcessRunner::finished, this,
            [this, runner, canonical](ProcessRunner::Outcome outcome, int,
                                      const QByteArray &, const QByteArray &stderrData,
                                      const QString &errorString) {
        if (runner != m_backupRunner)
            return;
        m_backupRunner = nullptr;
        runner->deleteLater();
        setBackupBusy(false);

        const QString details = QString::fromUtf8(stderrData).trimmed();
        if (outcome == ProcessRunner::Success) {
            setBackupResult(tr("Archivio verificato correttamente."), canonical,
                            QStringLiteral("success"));
            OperationLog::append(QStringLiteral("Backup"), QStringLiteral("verify"),
                                 QStringLiteral("success"), QFileInfo(canonical).fileName());
            return;
        }

        QString message;
        QString state = QStringLiteral("error");
        if (outcome == ProcessRunner::Cancelled) {
            message = tr("Verifica annullata.");
            state = QStringLiteral("cancelled");
        } else if (outcome == ProcessRunner::TimedOut) {
            message = tr("Tempo massimo superato durante la verifica dell'archivio.");
        } else if (outcome == ProcessRunner::FailedToStart) {
            message = tr("Impossibile avviare la verifica: %1").arg(errorString);
        } else {
            message = details.isEmpty() ? tr("Archivio non valido o danneggiato.") : details;
        }
        setBackupResult(message, canonical, state);
        OperationLog::append(QStringLiteral("Backup"), QStringLiteral("verify"),
                             state, QFileInfo(canonical).fileName());
    });

    ProcessRunner::Options options;
    options.program = tar;
    options.arguments = {QStringLiteral("-tzf"), canonical};
    options.timeoutMs = kBackupVerifyTimeoutMs;
    options.maxOutputBytes = 64 * 1024;
    options.mergedChannels = false;
    options.processGroup = true;
    if (!runner->start(options)) {
        m_backupRunner = nullptr;
        runner->deleteLater();
        setBackupBusy(false);
        setBackupResult(tr("Impossibile inizializzare la verifica dell'archivio."),
                        canonical, QStringLiteral("error"));
        return false;
    }
    return true;
}

bool SystemBackend::restoreSnapshot(const QString &path)
{
    if (m_backupBusy)
        return false;

    QString canonical;
    if (!validateBackupPath(path, &canonical)) {
        setBackupResult(tr("Archivio di backup non valido o fuori dalla cartella backup selezionata."),
                        QString(), QStringLiteral("error"));
        return false;
    }

    const QString tar = resolveExecutable(QStringLiteral("tar"));
    if (tar.isEmpty()) {
        setBackupResult(tr("tar non disponibile."), QString(), QStringLiteral("error"));
        return false;
    }

    setBackupBusy(true);
    setBackupResult(tr("Verifica preventiva archivio in corso…"), canonical,
                    QStringLiteral("running"));

    const auto startRestore = [this, canonical, tar]() {
        auto *runner = new ProcessRunner(this);
        m_backupRunner = runner;
        setBackupResult(tr("Ripristino in corso…"), canonical, QStringLiteral("running"));

        connect(runner, &ProcessRunner::finished, this,
                [this, runner, canonical](ProcessRunner::Outcome outcome, int exitCode,
                                          const QByteArray &stdoutData,
                                          const QByteArray &stderrData,
                                          const QString &errorString) {
            if (runner != m_backupRunner)
                return;
            m_backupRunner = nullptr;
            runner->deleteLater();
            setBackupBusy(false);

            QByteArray combined = stdoutData;
            if (!combined.isEmpty() && !stderrData.isEmpty() && !combined.endsWith('\n'))
                combined.append('\n');
            combined.append(stderrData);
            const QString details = QString::fromUtf8(combined).trimmed();

            if (outcome == ProcessRunner::Success) {
                setBackupResult(tr("Backup ripristinato. Disconnettersi o riavviare le applicazioni interessate per applicare tutte le configurazioni."),
                                canonical, QStringLiteral("success"));
                OperationLog::append(QStringLiteral("Backup"), QStringLiteral("restore"),
                                     QStringLiteral("success"), QFileInfo(canonical).fileName());
                notify(tr("Ripristino completato"), QFileInfo(canonical).fileName());
                return;
            }

            QString message;
            QString state = QStringLiteral("error");
            if (outcome == ProcessRunner::Cancelled) {
                message = tr("Ripristino annullato. Alcuni file potrebbero essere già stati ripristinati.");
                state = QStringLiteral("warning");
            } else if (outcome == ProcessRunner::TimedOut) {
                message = tr("Ripristino interrotto per timeout. Alcuni file potrebbero essere già stati ripristinati.");
                state = QStringLiteral("warning");
            } else if (outcome == ProcessRunner::FailedToStart) {
                message = tr("Impossibile avviare il ripristino: %1").arg(errorString);
            } else {
                message = details.isEmpty()
                    ? tr("Ripristino non riuscito (codice %1).").arg(exitCode)
                    : details;
            }
            setBackupResult(message, canonical, state);
            OperationLog::append(QStringLiteral("Backup"), QStringLiteral("restore"),
                                 outcome == ProcessRunner::TimedOut ? QStringLiteral("timeout")
                                                                   : state,
                                 QFileInfo(canonical).fileName());
        });

        ProcessRunner::Options options;
        options.program = tar;
        options.arguments = {QStringLiteral("-xzf"), canonical,
                             QStringLiteral("--no-same-owner"),
                             QStringLiteral("--no-same-permissions"),
                             QStringLiteral("-C"), QDir::homePath()};
        options.timeoutMs = kBackupOperationTimeoutMs;
        options.maxOutputBytes = 256 * 1024;
        options.mergedChannels = false;
        options.processGroup = true;
        if (!runner->start(options)) {
            m_backupRunner = nullptr;
            runner->deleteLater();
            setBackupBusy(false);
            setBackupResult(tr("Impossibile inizializzare il ripristino."),
                            canonical, QStringLiteral("error"));
            OperationLog::append(QStringLiteral("Backup"), QStringLiteral("restore"),
                                 QStringLiteral("error"), QFileInfo(canonical).fileName());
        }
    };

    auto *preflight = new ProcessRunner(this);
    m_backupRunner = preflight;
    connect(preflight, &ProcessRunner::finished, this,
            [this, preflight, canonical, startRestore](ProcessRunner::Outcome outcome, int,
                                                       const QByteArray &stdoutData,
                                                       const QByteArray &stderrData,
                                                       const QString &errorString) {
        if (preflight != m_backupRunner)
            return;
        m_backupRunner = nullptr;
        preflight->deleteLater();

        if (outcome == ProcessRunner::Success) {
            const QStringList members = QString::fromUtf8(stdoutData).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
            for (const QString &member : members) {
                if (!Validators::archiveMemberPath(member.trimmed())) {
                    setBackupBusy(false);
                    setBackupResult(tr("Ripristino bloccato: percorso archivio non sicuro: %1").arg(member.left(160)),
                                    canonical, QStringLiteral("error"));
                    OperationLog::append(QStringLiteral("Backup"), QStringLiteral("restore"),
                                         QStringLiteral("error"), QFileInfo(canonical).fileName());
                    return;
                }
            }

            auto *typeCheck = new ProcessRunner(this);
            m_backupRunner = typeCheck;
            connect(typeCheck, &ProcessRunner::finished, this,
                    [this, typeCheck, canonical, startRestore](ProcessRunner::Outcome typeOutcome, int,
                                                               const QByteArray &typeStdout,
                                                               const QByteArray &typeStderr,
                                                               const QString &typeError) {
                if (typeCheck != m_backupRunner)
                    return;
                m_backupRunner = nullptr;
                typeCheck->deleteLater();
                if (typeOutcome == ProcessRunner::Success) {
                    const QStringList entries = QString::fromUtf8(typeStdout).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
                    for (const QString &entry : entries) {
                        if (!Validators::archiveVerboseEntry(entry)) {
                            setBackupBusy(false);
                            setBackupResult(tr("Ripristino bloccato: tipo o collegamento archivio non sicuro."),
                                            canonical, QStringLiteral("error"));
                            OperationLog::append(QStringLiteral("Backup"), QStringLiteral("restore"),
                                                 QStringLiteral("error"), QFileInfo(canonical).fileName());
                            return;
                        }
                    }
                    startRestore();
                    return;
                }
                setBackupBusy(false);
                const QString details = QString::fromUtf8(typeStderr).trimmed();
                setBackupResult(details.isEmpty() ? tr("Ripristino bloccato: impossibile validare i tipi dei membri dell'archivio.")
                                                  : details,
                                canonical, QStringLiteral("error"));
                if (typeOutcome == ProcessRunner::FailedToStart && !typeError.isEmpty())
                    setBackupResult(tr("Impossibile avviare la verifica dei tipi: %1").arg(typeError), canonical, QStringLiteral("error"));
            });
            ProcessRunner::Options typeOptions;
            typeOptions.program = resolveExecutable(QStringLiteral("tar"));
            typeOptions.arguments = {QStringLiteral("-tvzf"), canonical, QStringLiteral("--numeric-owner")};
            typeOptions.timeoutMs = kBackupVerifyTimeoutMs;
            typeOptions.maxOutputBytes = 256 * 1024;
            typeOptions.mergedChannels = false;
            typeOptions.processGroup = true;
            if (!typeCheck->start(typeOptions)) {
                m_backupRunner = nullptr;
                typeCheck->deleteLater();
                setBackupBusy(false);
                setBackupResult(tr("Impossibile inizializzare la verifica dei tipi dell'archivio."), canonical, QStringLiteral("error"));
            }
            return;
        }

        setBackupBusy(false);
        const QString details = QString::fromUtf8(stderrData).trimmed();
        QString message;
        QString state = QStringLiteral("error");
        if (outcome == ProcessRunner::Cancelled) {
            message = tr("Ripristino annullato durante la verifica preventiva.");
            state = QStringLiteral("cancelled");
        } else if (outcome == ProcessRunner::TimedOut) {
            message = tr("Tempo massimo superato durante la verifica preventiva dell'archivio.");
        } else if (outcome == ProcessRunner::FailedToStart) {
            message = tr("Impossibile avviare la verifica preventiva: %1").arg(errorString);
        } else {
            message = details.isEmpty()
                ? tr("Ripristino bloccato: l'archivio non supera la verifica preventiva.")
                : details;
        }
        setBackupResult(message, canonical, state);
        OperationLog::append(QStringLiteral("Backup"), QStringLiteral("restore"),
                             state, QFileInfo(canonical).fileName());
    });

    ProcessRunner::Options preflightOptions;
    preflightOptions.program = tar;
    preflightOptions.arguments = {QStringLiteral("-tzf"), canonical};
    preflightOptions.timeoutMs = kBackupVerifyTimeoutMs;
    preflightOptions.maxOutputBytes = 64 * 1024;
    preflightOptions.mergedChannels = false;
    preflightOptions.processGroup = true;
    if (!preflight->start(preflightOptions)) {
        m_backupRunner = nullptr;
        preflight->deleteLater();
        setBackupBusy(false);
        setBackupResult(tr("Impossibile inizializzare la verifica preventiva."),
                        canonical, QStringLiteral("error"));
        return false;
    }
    return true;
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
    for (qsizetype i = lines.size(); i > 0 && emitted < 20; --i) {
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

    if (kind != QStringLiteral("home") && kind != QStringLiteral("config")) {
        setBackupResult(tr("Tipo di snapshot non consentito."), QString(), QStringLiteral("error"));
        return false;
    }

    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    const QString label = kind == QStringLiteral("home") ? QStringLiteral("home") : QStringLiteral("config");
    const QString output = backupDir.filePath(QStringLiteral("%1-%2.tar.gz").arg(label, stamp));
    const QString partial = output + QStringLiteral(".partial");
    QFile::remove(partial);
    {
        QFile partialFile(partial);
        if (!partialFile.open(QIODevice::WriteOnly)
            || !partialFile.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
            setBackupResult(tr("Impossibile creare il file parziale del backup con permessi sicuri."),
                            QString(), QStringLiteral("error"));
            return false;
        }
    }

    QStringList args = {QStringLiteral("-czf"), partial};
    if (kind == QStringLiteral("home")) {
        for (const QString &excluded : backupHomeExcludes())
            args << QStringLiteral("--exclude=./") + excluded;

        const QString canonicalHome = QFileInfo(home).canonicalFilePath();
        if (!canonicalHome.isEmpty() && canonicalBackupRoot == canonicalHome) {
            args << QStringLiteral("--exclude=./") + QFileInfo(partial).fileName();
            args << QStringLiteral("--exclude=./") + QFileInfo(output).fileName();
        } else if (!canonicalHome.isEmpty()
                   && canonicalBackupRoot.startsWith(canonicalHome + QLatin1Char('/'))) {
            const QString relativeBackup = QDir(canonicalHome).relativeFilePath(canonicalBackupRoot);
            if (!relativeBackup.isEmpty() && relativeBackup != QStringLiteral("."))
                args << QStringLiteral("--exclude=./") + relativeBackup;
        }
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
        setBackupBusy(false);

        QByteArray combined = stdoutData;
        if (!combined.isEmpty() && !stderrData.isEmpty() && !combined.endsWith('\n'))
            combined.append('\n');
        combined.append(stderrData);
        const QString details = QString::fromUtf8(combined).trimmed();

        const bool archiveProduced = QFileInfo(partial).exists() && QFileInfo(partial).size() > 0;
        if (outcome == ProcessRunner::Success && archiveProduced) {
            QFile::remove(output);
            if (!QFile::rename(partial, output)) {
                m_backupPartialPath = partial;
                setBackupResult(tr("Snapshot prodotto ma non è stato possibile finalizzarne il nome. Il file parziale è stato conservato."),
                                partial, QStringLiteral("error"));
                return;
            }
            m_backupPartialPath.clear();
            QFile::setPermissions(output, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
            setBackupResult(tr("Snapshot creato correttamente."), output, QStringLiteral("success"));
            OperationLog::append(QStringLiteral("Backup"), QStringLiteral("create"),
                                 QStringLiteral("success"), QFileInfo(output).fileName());
            notify(tr("Backup completato"), output);
            return;
        }

        QFile::remove(partial);
        m_backupPartialPath.clear();
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
                             outcome == ProcessRunner::TimedOut ? QStringLiteral("timeout")
                                                               : state,
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

bool SystemBackend::cancelSnapshot()
{
    return m_backupRunner && m_backupBusy && m_backupRunner->cancel();
}

bool SystemBackend::deleteSnapshot(const QString &path)
{
    if (m_backupBusy)
        return false;
    QString canonical;
    if (!validateBackupPath(path, &canonical))
        return false;
    const QString name = QFileInfo(canonical).fileName();
    if (!QFile::remove(canonical)) {
        setBackupResult(tr("Impossibile eliminare il backup: %1.").arg(name),
                        QString(), QStringLiteral("error"));
        return false;
    }
    OperationLog::append(QStringLiteral("Backup"), QStringLiteral("delete"),
                         QStringLiteral("success"), name);
    setBackupResult(tr("Backup eliminato."), QString(), QStringLiteral("success"));
    return true;
}

bool SystemBackend::openBackupFolder() const
{
    QString path = currentBackupRoot();
    if (path.isEmpty()) {
        path = m_backupDirectory.isEmpty() ? defaultBackupDirectory() : m_backupDirectory;
        if (!QDir().mkpath(path))
            return false;
    }
    return QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

QVariantList SystemBackend::backupPreview(const QString &kind) const
{
    QVariantList result;
    const QString home = QDir::homePath();

    if (kind == QStringLiteral("config")) {
        for (const QString &entry : backupConfigEntries()) {
            if (!QFileInfo::exists(home + QLatin1Char('/') + entry))
                continue;
            QVariantMap item;
            item.insert(QStringLiteral("path"), QStringLiteral("~/") + entry);
            result.append(item);
        }
        return result;
    }

    if (kind == QStringLiteral("home")) {
        QVariantMap allHome;
        allHome.insert(QStringLiteral("path"), QStringLiteral("~/"));
        result.append(allHome);
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

void SystemBackend::setResourceMonitoringEnabled(bool enabled)
{
    if (m_resourceMonitoringEnabled == enabled)
        return;

    m_resourceMonitoringEnabled = enabled;
    if (!enabled) {
        m_resourceTimer->stop();
        m_previousCpuTotal = 0;
        m_previousCpuIdle = 0;
        return;
    }

    m_previousCpuTotal = 0;
    m_previousCpuIdle = 0;
    m_lastTopMemoryRefreshMs = 0;
    if (m_cpuUsagePercent != -1) {
        m_cpuUsagePercent = -1;
        emit resourcesChanged();
    }
    refreshResources();
    m_resourceTimer->start();
}

void SystemBackend::refreshResources()
{
    if (!m_resourceMonitoringEnabled)
        return;
    int nextCpuUsage = m_cpuUsagePercent;
    QFile stat(QStringLiteral("/proc/stat"));
    if (stat.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QList<QByteArray> parts = stat.readLine().simplified().split(' ');
        if (parts.size() >= 9 && parts.at(0) == "cpu") {
            bool ok = true;
            quint64 values[8] = {};
            for (int i = 0; i < 8; ++i) {
                bool fieldOk = false;
                values[i] = parts.at(i + 1).toULongLong(&fieldOk);
                ok = ok && fieldOk;
            }
            if (ok) {
                const quint64 total = values[0] + values[1] + values[2] + values[3]
                                    + values[4] + values[5] + values[6] + values[7];
                const quint64 idle = values[3] + values[4];
                if (m_previousCpuTotal > 0 && total > m_previousCpuTotal) {
                    const quint64 totalDelta = total - m_previousCpuTotal;
                    const quint64 idleDelta = idle >= m_previousCpuIdle ? idle - m_previousCpuIdle : 0;
                    const double busy = totalDelta > 0
                        ? 100.0 * double(totalDelta - qMin(idleDelta, totalDelta)) / double(totalDelta)
                        : 0.0;
                    nextCpuUsage = qBound(0, qRound(busy), 100);
                }
                m_previousCpuTotal = total;
                m_previousCpuIdle = idle;
            }
        }
    }

    qint64 totalKiB = -1;
    qint64 availableKiB = -1;
    QFile meminfo(QStringLiteral("/proc/meminfo"));
    if (meminfo.open(QIODevice::ReadOnly | QIODevice::Text)) {
        for (const QByteArray &rawLine : meminfo.readAll().split('\n')) {
            const QByteArray line = rawLine.simplified();
            if (line.startsWith("MemTotal:")) {
                const QList<QByteArray> parts = line.split(' ');
                if (parts.size() >= 2)
                    totalKiB = parts.at(1).toLongLong();
            } else if (line.startsWith("MemAvailable:")) {
                const QList<QByteArray> parts = line.split(' ');
                if (parts.size() >= 2)
                    availableKiB = parts.at(1).toLongLong();
            }
        }
    }

    const qint64 nextTotalMiB = totalKiB >= 0 ? totalKiB / 1024 : -1;
    const qint64 nextUsedMiB = totalKiB >= 0 && availableKiB >= 0
        ? qMax<qint64>(0, totalKiB - availableKiB) / 1024
        : -1;
    const double nextTemperature = readCpuTemperature();

    const bool changed = nextCpuUsage != m_cpuUsagePercent
        || nextUsedMiB != m_memoryUsedMiB
        || nextTotalMiB != m_memoryTotalMiB
        || !qFuzzyCompare(nextTemperature + 1.0, m_cpuTemperatureC + 1.0);

    m_cpuUsagePercent = nextCpuUsage;
    m_memoryUsedMiB = nextUsedMiB;
    m_memoryTotalMiB = nextTotalMiB;
    m_cpuTemperatureC = nextTemperature;
    if (changed)
        emit resourcesChanged();

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (m_lastTopMemoryRefreshMs == 0 || nowMs - m_lastTopMemoryRefreshMs >= 6000) {
        refreshTopMemoryProcesses();
        m_lastTopMemoryRefreshMs = nowMs;
    }
}

double SystemBackend::readCpuTemperature() const
{
    const QDir hwmonRoot(QStringLiteral("/sys/class/hwmon"));
    const QStringList hwmons = hwmonRoot.entryList(
        QStringList{QStringLiteral("hwmon*")}, QDir::Dirs | QDir::NoDotAndDotDot);

    int bestScore = -1;
    double bestTemperature = -1.0;
    for (const QString &directoryName : hwmons) {
        const QDir directory(hwmonRoot.filePath(directoryName));
        QFile nameFile(directory.filePath(QStringLiteral("name")));
        QString sensorName;
        if (nameFile.open(QIODevice::ReadOnly | QIODevice::Text))
            sensorName = QString::fromUtf8(nameFile.readAll()).trimmed().toLower();

        int baseScore = -1;
        if (sensorName == QStringLiteral("k10temp")
            || sensorName == QStringLiteral("coretemp")
            || sensorName == QStringLiteral("zenpower"))
            baseScore = 100;
        else if (sensorName.contains(QStringLiteral("cpu")))
            baseScore = 70;

        const QStringList inputs = directory.entryList(
            QStringList{QStringLiteral("temp*_input")}, QDir::Files);
        for (const QString &inputName : inputs) {
            QFile input(directory.filePath(inputName));
            if (!input.open(QIODevice::ReadOnly | QIODevice::Text))
                continue;
            bool ok = false;
            const qint64 milli = QString::fromUtf8(input.readAll()).trimmed().toLongLong(&ok);
            if (!ok || milli < -20000 || milli > 150000)
                continue;

            QString label;
            const QString labelName = inputName;
            const QString prefix = labelName.left(labelName.indexOf(QLatin1Char('_')));
            QFile labelFile(directory.filePath(prefix + QStringLiteral("_label")));
            if (labelFile.open(QIODevice::ReadOnly | QIODevice::Text))
                label = QString::fromUtf8(labelFile.readAll()).trimmed().toLower();

            int score = baseScore;
            if (label.contains(QStringLiteral("tctl"))
                || label.contains(QStringLiteral("tdie"))
                || label.contains(QStringLiteral("package"))
                || label.contains(QStringLiteral("cpu")))
                score = qMax(score, 80);
            if (score < 0)
                continue;

            const double temperature = double(milli) / 1000.0;
            if (score > bestScore) {
                bestScore = score;
                bestTemperature = temperature;
            }
        }
    }
    return bestTemperature;
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
