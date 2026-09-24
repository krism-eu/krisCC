#include "PackageSearch.h"
#include "ContractParsers.h"

#include <QFile>
#include <QFileInfo>
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

    if (m_truncated) {
        m_truncated = false;
        emit truncatedChanged();
    }
    setSearching(true);
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

        const auto parsed = ContractParsers::parseDnfRepoquery(stdoutData);
        if (!parsed.ok()) {
            clearResults();
            setSearching(false);
            emit searchError(tr("Formato di output DNF5 repoquery non riconosciuto."));
            emit searchFinished();
            return;
        }

        QList<Entry> entries;
        bool truncated = false;
        for (const QVariant &value : parsed.values) {
            if (entries.size() >= 100) {
                truncated = true;
                break;
            }
            const QVariantMap row = value.toMap();
            Entry entry;
            entry.name = row.value(QStringLiteral("name")).toString();
            entry.summary = row.value(QStringLiteral("summary")).toString();
            entry.version = row.value(QStringLiteral("version")).toString();
            entry.repository = row.value(QStringLiteral("repository")).toString();
            entry.arch = row.value(QStringLiteral("arch")).toString();
            entry.downloadSize = row.value(QStringLiteral("downloadSize")).toULongLong();
            entry.installSize = row.value(QStringLiteral("installSize")).toULongLong();
            entry.installed = m_installed.contains(entry.name);
            entry.owned = m_owned.contains(entry.name);
            entry.persistent = m_persistent.contains(entry.name);
            entries.append(entry);
        }

        beginResetModel();
        m_results = entries;
        endResetModel();
        if (m_truncated != truncated) {
            m_truncated = truncated;
            emit truncatedChanged();
        }
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

        const auto parsed = ContractParsers::parseDnfListJson(stdoutData);
        if (!parsed.ok()) {
            setSearching(false);
            emit searchError(tr("Formato JSON DNF5 non riconosciuto."));
            emit searchFinished();
            return;
        }

        QList<Entry> entries;
        for (const QVariant &value : parsed.values) {
            const QVariantMap row = value.toMap();
            Entry entry;
            entry.name = row.value(QStringLiteral("name")).toString();
            entry.arch = row.value(QStringLiteral("arch")).toString();
            entry.version = row.value(QStringLiteral("version")).toString();
            entry.repository = row.value(QStringLiteral("repository")).toString();
            entry.installed = installedEntries || m_installed.contains(entry.name);
            entry.owned = m_owned.contains(entry.name);
            entry.persistent = m_persistent.contains(entry.name);

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
        }

        if (installedEntries) {
            m_sourceResults = entries;
            applyLocalFilter();
        } else {
            beginResetModel();
            m_results = entries;
            endResetModel();
            emit countChanged();
        }
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
    m_sourceResults.clear();
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

void PackageSearch::setLocalFilter(const QString &text)
{
    const QString next = text.trimmed();
    if (m_localFilter == next)
        return;
    m_localFilter = next;
    applyLocalFilter();
}

void PackageSearch::applyLocalFilter()
{
    QList<Entry> filtered;
    if (m_localFilter.isEmpty()) {
        filtered = m_sourceResults;
    } else {
        for (const Entry &entry : m_sourceResults) {
            const bool matches =
                entry.name.contains(m_localFilter, Qt::CaseInsensitive)
                || entry.version.contains(m_localFilter, Qt::CaseInsensitive)
                || entry.repository.contains(m_localFilter, Qt::CaseInsensitive)
                || entry.arch.contains(m_localFilter, Qt::CaseInsensitive);
            if (matches)
                filtered.append(entry);
        }
    }

    beginResetModel();
    m_results = filtered;
    endResetModel();
    emit countChanged();
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
