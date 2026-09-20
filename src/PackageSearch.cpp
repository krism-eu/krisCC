#include "PackageSearch.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QTextStream>


PackageSearch::PackageSearch(QObject *parent)
    : QAbstractListModel(parent)
{
    QFile file(QStringLiteral("/usr/share/krisos/owned-packages.txt"));
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&file);
        while (!in.atEnd()) {
            const QString name = in.readLine().trimmed();
            if (!name.isEmpty() && !name.startsWith(QLatin1Char('#')))
                m_owned.insert(name);
        }
    }
    refreshPersistentSet();
}

int PackageSearch::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_results.size();
}

QVariant PackageSearch::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_results.size())
        return {};

    const Entry &entry = m_results.at(index.row());
    switch (role) {
    case NameRole:         return entry.name;
    case SummaryRole:      return entry.summary;
    case InstalledRole:    return entry.installed;
    case OwnedRole:        return entry.owned;
    case PersistentRole:   return entry.persistent;
    case VersionRole:      return entry.version;
    case RepositoryRole:   return entry.repository;
    case ArchRole:         return entry.arch;
    case DownloadSizeRole: return QVariant::fromValue<qulonglong>(entry.downloadSize);
    case InstallSizeRole:  return QVariant::fromValue<qulonglong>(entry.installSize);
    default:               return {};
    }
}

QHash<int, QByteArray> PackageSearch::roleNames() const
{
    return {
        {NameRole, "name"},
        {SummaryRole, "summary"},
        {InstalledRole, "installed"},
        {OwnedRole, "owned"},
        {PersistentRole, "persistent"},
        {VersionRole, "version"},
        {RepositoryRole, "repository"},
        {ArchRole, "arch"},
        {DownloadSizeRole, "downloadSize"},
        {InstallSizeRole, "installSize"}
    };
}

void PackageSearch::search(const QString &term)
{
    const QString sanitized = sanitizeTerm(term);
    refreshPersistentSet();
    ++m_generation;
    stopActiveProcess();

    if (sanitized.size() < 2) {
        clearResults();
        setSearching(false);
        emit searchFinished();
        return;
    }

    setSearching(true);
    if (installedCacheCurrent())
        startRepoQuery(sanitized);
    else
        startInstalledQuery(sanitized);
}

void PackageSearch::loadInstalled(const QString &filter)
{
    static const QSet<QString> allowed = {
        QStringLiteral("all"), QStringLiteral("base"),
        QStringLiteral("persistent"), QStringLiteral("local")
    };
    m_installedFilter = allowed.contains(filter) ? filter : QStringLiteral("all");
    refreshPersistentSet();
    ++m_generation;
    stopActiveProcess();
    startListQuery(QStringLiteral("--installed"), true);
}

void PackageSearch::loadUpgrades()
{
    refreshPersistentSet();
    ++m_generation;
    stopActiveProcess();
    startListQuery(QStringLiteral("--upgrades"), true);
}

void PackageSearch::loadRecent()
{
    refreshPersistentSet();
    ++m_generation;
    stopActiveProcess();
    startListQuery(QStringLiteral("--recent"), false);
}

void PackageSearch::startInstalledQuery(const QString &term)
{
    const quint64 generation = m_generation;
    auto *rawProcess = new QProcess(this);
    const QPointer<QProcess> process(rawProcess);
    m_process = rawProcess;

    connect(rawProcess, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this, process, term, generation](int exitCode, QProcess::ExitStatus status) {
        if (!process)
            return;
        if (process != m_process || generation != m_generation) {
            process->deleteLater();
            return;
        }

        const bool timedOut = process->property("krisccTimedOut").toBool();
        m_installed.clear();
        if (!timedOut && status == QProcess::NormalExit && exitCode == 0) {
            const auto lines = process->readAllStandardOutput().split('\n');
            for (const QByteArray &line : lines) {
                const QString name = QString::fromUtf8(line).trimmed();
                if (!name.isEmpty())
                    m_installed.insert(name);
            }

            m_installedCacheDbPath = rpmDatabasePath();
            const QFileInfo dbInfo(m_installedCacheDbPath);
            m_installedCacheMtime = dbInfo.exists() ? dbInfo.lastModified() : QDateTime();
            m_installedCacheValid = true;
        } else {
            m_installedCacheValid = false;
        }

        m_process = nullptr;
        process->deleteLater();
        if (timedOut)
            emit searchError(tr("Tempo massimo superato durante la lettura dei pacchetti installati; la ricerca continua senza cache locale aggiornata."));
        startRepoQuery(term);
    });

    connect(rawProcess, &QProcess::errorOccurred, this,
            [this, process, term, generation](QProcess::ProcessError error) {
        if (!process || process != m_process || generation != m_generation
            || error != QProcess::FailedToStart)
            return;

        m_installed.clear();
        m_installedCacheValid = false;
        m_process = nullptr;
        process->deleteLater();
        emit searchError(tr("Impossibile avviare rpm per leggere i pacchetti installati."));
        startRepoQuery(term);
    });

    rawProcess->start(QStringLiteral("/usr/bin/rpm"),
                      {QStringLiteral("-qa"), QStringLiteral("--qf"), QStringLiteral("%{NAME}\\n")});
    QTimer::singleShot(30 * 1000, rawProcess, [this, process, generation] {
        if (!process || process != m_process || generation != m_generation
                || process->state() == QProcess::NotRunning)
            return;
        process->setProperty("krisccTimedOut", true);
        process->terminate();
        QTimer::singleShot(2000, process, [process] {
            if (process && process->state() != QProcess::NotRunning)
                process->kill();
        });
    });
}

void PackageSearch::startRepoQuery(const QString &term)
{
    const quint64 generation = m_generation;
    auto *rawProcess = new QProcess(this);
    const QPointer<QProcess> process(rawProcess);
    m_process = rawProcess;

    connect(rawProcess, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this, process, generation](int exitCode, QProcess::ExitStatus status) {
        if (!process)
            return;
        if (process != m_process || generation != m_generation) {
            process->deleteLater();
            return;
        }

        const bool timedOut = process->property("krisccTimedOut").toBool();
        const QByteArray stdoutData = process->readAllStandardOutput();
        const QString stderrText = QString::fromUtf8(process->readAllStandardError()).trimmed();
        m_process = nullptr;
        process->deleteLater();

        if (timedOut || status != QProcess::NormalExit || exitCode != 0) {
            clearResults();
            setSearching(false);
            emit searchError(timedOut ? tr("Tempo massimo superato durante la ricerca DNF5.")
                                      : (stderrText.isEmpty() ? tr("La ricerca dnf5 non e' riuscita.") : stderrText));
            emit searchFinished();
            return;
        }

        QList<Entry> entries;
        QSet<QString> seen;
        bool contractInvalid = false;
        const auto lines = QString::fromUtf8(stdoutData).split('\n', Qt::SkipEmptyParts);
        for (const QString &line : lines) {
            const QStringList parts = line.split(QLatin1Char('\t'));
            if (parts.size() != 7) {
                contractInvalid = true;
                break;
            }

            const QString name = parts.at(0).trimmed();
            const QString arch = parts.at(4).trimmed();
            bool downloadOk = false;
            bool installOk = false;
            const quint64 downloadSize = parts.at(5).toULongLong(&downloadOk);
            const quint64 installSize = parts.at(6).toULongLong(&installOk);
            if (name.isEmpty() || arch.isEmpty() || !downloadOk || !installOk) {
                contractInvalid = true;
                break;
            }

            const QString key = name + QLatin1Char('\x1f') + arch;
            if (seen.contains(key))
                continue;

            seen.insert(key);
            Entry entry;
            entry.name = name;
            entry.summary = parts.at(1).simplified().left(512);
            entry.version = parts.at(2).trimmed();
            entry.repository = parts.at(3).trimmed();
            entry.arch = arch;
            entry.downloadSize = downloadSize;
            entry.installSize = installSize;
            entry.installed = m_installed.contains(name);
            entry.owned = m_owned.contains(name);
            entry.persistent = m_persistent.contains(name);
            entries.append(entry);
            if (entries.size() >= 200)
                break;
        }

        if (contractInvalid) {
            clearResults();
            setSearching(false);
            emit searchError(tr("Formato di output DNF5 repoquery non riconosciuto."));
            emit searchFinished();
            return;
        }

        beginResetModel();
        m_results = entries;
        endResetModel();
        emit countChanged();
        setSearching(false);
        emit searchFinished();
    });

    connect(rawProcess, &QProcess::errorOccurred, this,
            [this, process, generation](QProcess::ProcessError error) {
        if (!process || process != m_process || generation != m_generation
            || error != QProcess::FailedToStart)
            return;
        m_process = nullptr;
        process->deleteLater();
        clearResults();
        setSearching(false);
        emit searchError(tr("Impossibile avviare dnf5."));
        emit searchFinished();
    });

    const QString packageSpec = QStringLiteral("*") + term + QStringLiteral("*");
    rawProcess->start(QStringLiteral("/usr/bin/dnf5"),
                      {QStringLiteral("repoquery"), QStringLiteral("--available"),
                       QStringLiteral("--latest-limit=1"),
                       QStringLiteral("--queryformat"),
                       QStringLiteral("%{name}\t%{summary}\t%{evr}\t%{repoid}\t%{arch}\t%{downloadsize}\t%{installsize}\n"),
                       packageSpec});
    QTimer::singleShot(2 * 60 * 1000, rawProcess, [this, process, generation] {
        if (!process || process != m_process || generation != m_generation
                || process->state() == QProcess::NotRunning)
            return;
        process->setProperty("krisccTimedOut", true);
        process->terminate();
        QTimer::singleShot(2000, process, [process] {
            if (process && process->state() != QProcess::NotRunning)
                process->kill();
        });
    });
}

void PackageSearch::startListQuery(const QString &filter, bool installedEntries)
{
    const quint64 generation = m_generation;
    setSearching(true);
    clearResults();

    auto *rawProcess = new QProcess(this);
    const QPointer<QProcess> process(rawProcess);
    m_process = rawProcess;
    rawProcess->setProcessChannelMode(QProcess::SeparateChannels);

    connect(rawProcess, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this, process, generation, installedEntries](int exitCode, QProcess::ExitStatus status) {
        if (!process)
            return;
        if (process != m_process || generation != m_generation) {
            process->deleteLater();
            return;
        }

        const bool timedOut = process->property("krisccTimedOut").toBool();
        const QByteArray stdoutData = process->readAllStandardOutput();
        const QString stderrText = QString::fromUtf8(process->readAllStandardError()).trimmed();
        m_process = nullptr;
        process->deleteLater();

        if (timedOut || status != QProcess::NormalExit || exitCode != 0) {
            setSearching(false);
            emit searchError(timedOut ? tr("Tempo massimo superato durante la lettura dell'elenco DNF5.")
                                      : (stderrText.isEmpty() ? tr("Impossibile leggere l'elenco pacchetti DNF5.") : stderrText));
            emit searchFinished();
            return;
        }

        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(stdoutData, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            setSearching(false);
            emit searchError(tr("Output JSON DNF5 non valido: %1").arg(parseError.errorString()));
            emit searchFinished();
            return;
        }

        QList<Entry> entries;
        QSet<QString> seen;
        bool sawArray = false;
        bool contractInvalid = false;
        const QJsonObject root = document.object();
        for (auto it = root.constBegin(); it != root.constEnd(); ++it) {
            if (!it.value().isArray())
                continue;
            sawArray = true;
            for (const QJsonValue &value : it.value().toArray()) {
                if (!value.isObject()) {
                    contractInvalid = true;
                    break;
                }
                const QJsonObject object = value.toObject();
                if (!object.value(QStringLiteral("name")).isString()
                    || !object.value(QStringLiteral("arch")).isString()
                    || !object.value(QStringLiteral("evr")).isString()
                    || !object.value(QStringLiteral("repository")).isString()) {
                    contractInvalid = true;
                    break;
                }

                const QString name = object.value(QStringLiteral("name")).toString().trimmed();
                const QString arch = object.value(QStringLiteral("arch")).toString().trimmed();
                if (name.isEmpty() || arch.isEmpty()) {
                    contractInvalid = true;
                    break;
                }

                const QString key = name + QLatin1Char('\x1f') + arch;
                if (seen.contains(key))
                    continue;
                seen.insert(key);

                Entry entry;
                entry.name = name;
                entry.arch = arch;
                entry.version = object.value(QStringLiteral("evr")).toString();
                entry.repository = object.value(QStringLiteral("repository")).toString();
                entry.installed = installedEntries || m_installed.contains(name);
                entry.owned = m_owned.contains(name);
                entry.persistent = m_persistent.contains(name);

                if (installedEntries) {
                    const bool local = entry.installed && !entry.owned && !entry.persistent;
                    if (m_installedFilter == QStringLiteral("base") && !entry.owned)
                        continue;
                    if (m_installedFilter == QStringLiteral("persistent") && !entry.persistent)
                        continue;
                    if (m_installedFilter == QStringLiteral("local") && !local)
                        continue;
                }

                entries.append(entry);
                if (entries.size() >= 500)
                    break;
            }
            if (contractInvalid || entries.size() >= 500)
                break;
        }

        if (!sawArray || contractInvalid) {
            setSearching(false);
            emit searchError(tr("Formato JSON DNF5 non riconosciuto."));
            emit searchFinished();
            return;
        }

        beginResetModel();
        m_results = entries;
        endResetModel();
        emit countChanged();
        setSearching(false);
        emit searchFinished();
    });

    connect(rawProcess, &QProcess::errorOccurred, this,
            [this, process, generation](QProcess::ProcessError error) {
        if (!process || process != m_process || generation != m_generation
            || error != QProcess::FailedToStart)
            return;
        m_process = nullptr;
        process->deleteLater();
        setSearching(false);
        emit searchError(tr("Impossibile avviare dnf5."));
        emit searchFinished();
    });

    QStringList args;
    args << QStringLiteral("list") << filter << QStringLiteral("--json");
    rawProcess->start(QStringLiteral("/usr/bin/dnf5"), args);
    QTimer::singleShot(2 * 60 * 1000, rawProcess, [this, process, generation] {
        if (!process || process != m_process || generation != m_generation
                || process->state() == QProcess::NotRunning)
            return;
        process->setProperty("krisccTimedOut", true);
        process->terminate();
        QTimer::singleShot(2000, process, [process] {
            if (process && process->state() != QProcess::NotRunning)
                process->kill();
        });
    });
}

void PackageSearch::clearResults()
{
    if (m_results.isEmpty())
        return;
    beginResetModel();
    m_results.clear();
    endResetModel();
    emit countChanged();
}

void PackageSearch::setSearching(bool searching)
{
    if (m_searching == searching)
        return;
    m_searching = searching;
    emit searchingChanged();
}

void PackageSearch::stopActiveProcess()
{
    if (!m_process)
        return;

    const QPointer<QProcess> process = m_process;
    QProcess *const rawProcess = process.data();
    m_process = nullptr;
    if (!rawProcess)
        return;

    disconnect(rawProcess, nullptr, this, nullptr);
    if (rawProcess->state() == QProcess::NotRunning) {
        rawProcess->deleteLater();
        return;
    }

    rawProcess->terminate();
    connect(rawProcess, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            rawProcess, &QObject::deleteLater);
    QTimer::singleShot(3000, rawProcess, [process]() {
        if (process && process->state() != QProcess::NotRunning)
            process->kill();
    });
}

void PackageSearch::refreshPersistentSet()
{
    m_persistent.clear();
    QFile file(QStringLiteral("/var/lib/krisos/packages.list"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return;
    while (!file.atEnd()) {
        const QString line = QString::fromUtf8(file.readLine()).trimmed();
        if (!line.isEmpty() && !line.startsWith(QLatin1Char('#')))
            m_persistent.insert(line);
    }
}

bool PackageSearch::installedCacheCurrent() const
{
    if (!m_installedCacheValid)
        return false;
    const QString dbPath = rpmDatabasePath();
    if (dbPath.isEmpty() || dbPath != m_installedCacheDbPath)
        return false;
    const QFileInfo info(dbPath);
    return info.exists() && info.lastModified() == m_installedCacheMtime;
}

QString PackageSearch::rpmDatabasePath() const
{
    static const QStringList candidates = {
        QStringLiteral("/usr/lib/sysimage/rpm/rpmdb.sqlite"),
        QStringLiteral("/var/lib/rpm/rpmdb.sqlite")
    };
    for (const QString &path : candidates) {
        if (QFileInfo::exists(path))
            return path;
    }
    return {};
}

QString PackageSearch::sanitizeTerm(const QString &term)
{
    QString out;
    out.reserve(term.size());
    bool pendingSeparator = false;

    for (const QChar ch : term.trimmed()) {
        if (ch.isLetterOrNumber() || QStringLiteral("._+:-").contains(ch)) {
            if (pendingSeparator && !out.isEmpty())
                out += QLatin1Char('*');
            out += ch;
            pendingSeparator = false;
        } else if (ch.isSpace()) {
            pendingSeparator = true;
        }
    }
    return out.left(128);
}
