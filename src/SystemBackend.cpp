#include "SystemBackend.h"

#include "OperationLog.h"
#include "PolkitHelper.h"
#include "Validators.h"
#include "ContractParsers.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QDateTime>
#include <QDBusInterface>
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
        QStringLiteral("cockpit.socket")
    };
    return services;
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

void SystemBackend::applyUefiEntriesOutput(const QString &output)
{
    const auto parsed = ContractParsers::parseUefiEntries(output.toUtf8());
    m_nextUefiBootLabel.clear();
    m_currentUefiBootCode.clear();
    m_uefiBootOrder.clear();

    const QRegularExpression bootCurrentPattern(
        QStringLiteral("(?m)^BootCurrent:\\s*([0-9A-Fa-f]{4})\\s*$"));
    const QRegularExpression bootNextPattern(
        QStringLiteral("(?m)^BootNext:\\s*([0-9A-Fa-f]{4})\\s*$"));
    const QRegularExpression bootOrderPattern(
        QStringLiteral("(?m)^BootOrder:\\s*([0-9A-Fa-f]{4}(?:,[0-9A-Fa-f]{4})*)\\s*$"));

    const QRegularExpressionMatch currentMatch = bootCurrentPattern.match(output);
    if (currentMatch.hasMatch())
        m_currentUefiBootCode = currentMatch.captured(1).toUpper();

    QString nextCode;
    const QRegularExpressionMatch nextMatch = bootNextPattern.match(output);
    if (nextMatch.hasMatch())
        nextCode = nextMatch.captured(1).toUpper();

    const QRegularExpressionMatch orderMatch = bootOrderPattern.match(output);
    if (orderMatch.hasMatch()) {
        for (const QString &token : orderMatch.captured(1).split(QLatin1Char(',')))
            m_uefiBootOrder.append(token.toUpper());
    }

    QHash<QString, QVariantMap> byCode;
    for (const QVariant &value : parsed.values) {
        QVariantMap row = value.toMap();
        const QString code = row.value(QStringLiteral("code")).toString();
        row.insert(QStringLiteral("current"), code == m_currentUefiBootCode);
        row.insert(QStringLiteral("next"), code == nextCode);
        row.insert(QStringLiteral("orderIndex"), m_uefiBootOrder.indexOf(code));
        byCode.insert(code, row);
        if (code == nextCode)
            m_nextUefiBootLabel = row.value(QStringLiteral("label")).toString();
    }

    QVariantList ordered;
    for (const QString &code : m_uefiBootOrder) {
        if (byCode.contains(code))
            ordered.append(byCode.take(code));
    }
    QStringList remaining = byCode.keys();
    remaining.sort();
    for (const QString &code : remaining)
        ordered.append(byCode.value(code));
    m_uefiEntries = ordered;

    if (m_nextUefiBootLabel.isEmpty() && !nextCode.isEmpty())
        m_nextUefiBootLabel = nextCode;
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

bool SystemBackend::clearNextUefi()
{
    if (!m_polkit || m_polkit->running() || !uefiBootAvailable())
        return false;

    m_bootSelectionOwned = true;
    m_bootSelectionRunning = true;
    m_bootSelectionKind = QStringLiteral("uefi");
    m_bootSelectionState = QStringLiteral("running");
    emit bootSelectionStateChanged();
    m_polkit->execute(QStringLiteral("/usr/libexec/kriscc/admin"),
                      {QStringLiteral("boot-clear-next-uefi")});
    return true;
}

bool SystemBackend::deleteUefiEntry(const QString &value)
{
    if (!m_polkit || m_polkit->running() || !uefiBootAvailable())
        return false;
    const QString token = value.trimmed().toUpper();
    if (!Validators::bootToken(token) || token == m_currentUefiBootCode)
        return false;

    bool exists = false;
    for (const QVariant &entry : m_uefiEntries) {
        if (entry.toMap().value(QStringLiteral("code")).toString() == token) {
            exists = true;
            break;
        }
    }
    if (!exists)
        return false;

    m_bootSelectionOwned = true;
    m_bootSelectionRunning = true;
    m_bootSelectionKind = QStringLiteral("uefi");
    m_bootSelectionState = QStringLiteral("running");
    emit bootSelectionStateChanged();
    m_polkit->execute(QStringLiteral("/usr/libexec/kriscc/admin"),
                      {QStringLiteral("boot-delete-uefi"), token});
    return true;
}

bool SystemBackend::applyUefiBootOrder(const QStringList &requestedOrder)
{
    if (!m_polkit || m_polkit->running() || !uefiBootAvailable())
        return false;

    QStringList order;
    for (const QString &value : requestedOrder)
        order.append(value.trimmed().toUpper());

    if (order.size() != m_uefiBootOrder.size())
        return false;

    QSet<QString> requested(order.cbegin(), order.cend());
    QSet<QString> current(m_uefiBootOrder.cbegin(), m_uefiBootOrder.cend());
    if (requested != current)
        return false;

    const QString serialized = order.join(QLatin1Char(','));
    if (!Validators::bootOrder(serialized))
        return false;

    m_bootSelectionOwned = true;
    m_bootSelectionRunning = true;
    m_bootSelectionKind = QStringLiteral("uefi");
    m_bootSelectionState = QStringLiteral("running");
    emit bootSelectionStateChanged();
    m_polkit->execute(QStringLiteral("/usr/libexec/kriscc/admin"),
                      {QStringLiteral("boot-order-uefi"), serialized});
    return true;
}

bool SystemBackend::moveUefiEntry(const QString &value, int direction)
{
    if (direction != -1 && direction != 1)
        return false;
    const QString token = value.trimmed().toUpper();
    const int index = m_uefiBootOrder.indexOf(token);
    const int target = index + direction;
    if (index < 0 || target < 0 || target >= m_uefiBootOrder.size())
        return false;

    QStringList order = m_uefiBootOrder;
    order.swapItemsAt(index, target);
    return applyUefiBootOrder(order);
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

QString SystemBackend::saveSupportReport(const QString &text) const
{
    if (text.trimmed().isEmpty() || text.size() > 2 * 1024 * 1024)
        return {};

    QString directory = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (directory.isEmpty())
        directory = QDir::homePath();
    if (!QDir().mkpath(directory))
        return {};

    const QString fileName = QStringLiteral("krisCC-support-%1.txt")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")));
    const QString path = QDir(directory).filePath(fileName);

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::NewOnly))
        return {};
    if (!file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner))
        return {};
    const QByteArray data = text.toUtf8();
    if (file.write(data) != data.size())
        return {};
    file.close();
    notify(tr("Rapporto supporto salvato"), path);
    return path;
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
        {QStringLiteral("backintime"), QStringLiteral("backintime-qt")},
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

bool SystemBackend::setWifiRadio(bool enabled, bool restartAfter)
{
    QDBusInterface properties(QStringLiteral("org.freedesktop.NetworkManager"),
                              QStringLiteral("/org/freedesktop/NetworkManager"),
                              QStringLiteral("org.freedesktop.DBus.Properties"),
                              QDBusConnection::systemBus());
    if (!properties.isValid()) {
        notify(tr("Wi-Fi non disponibile"), tr("NetworkManager non è disponibile sul bus di sistema."));
        return false;
    }
    properties.setInteractiveAuthorizationAllowed(true);
    auto *watcher = new QDBusPendingCallWatcher(
        properties.asyncCall(QStringLiteral("Set"),
                             QStringLiteral("org.freedesktop.NetworkManager"),
                             QStringLiteral("WirelessEnabled"),
                             QVariant::fromValue(QDBusVariant(enabled))),
        this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, enabled, restartAfter](QDBusPendingCallWatcher *call) {
        const QDBusPendingReply<> reply(*call);
        call->deleteLater();
        if (reply.isError()) {
            notify(tr("Operazione Wi-Fi non riuscita"), reply.error().message());
            QTimer::singleShot(250, this, &SystemBackend::refreshServiceStates);
            return;
        }
        if (restartAfter && !enabled) {
            QTimer::singleShot(350, this, [this] { setWifiRadio(true, false); });
            return;
        }
        notify(enabled ? tr("Wi-Fi attivato") : tr("Wi-Fi disattivato"));
        QTimer::singleShot(350, this, &SystemBackend::refreshServiceStates);
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
