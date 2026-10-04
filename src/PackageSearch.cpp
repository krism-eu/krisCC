#include "PackageSearch.h"

#include "ContractParsers.h"
#include "PackageInventoryCache.h"

#include <QTimer>

PackageSearch::PackageSearch(QObject *parent)
    : QAbstractListModel(parent)
    , m_inventory(PackageInventoryCache::shared())
{
    connect(m_inventory, &PackageInventoryCache::refreshFinished, this,
            [this](bool success, const QString &error) { onInventoryReady(success, error); });
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

void PackageSearch::invalidateSharedInventory()
{
    PackageInventoryCache::shared()->invalidate();
}

void PackageSearch::refreshInventory()
{
    m_inventory->ensureFresh(true);
}

void PackageSearch::requestInventory(PendingQuery query, const QString &value, bool force)
{
    m_pendingQuery = query;
    m_pendingValue = value;
    m_pendingGeneration = m_generation;
    m_inventory->ensureFresh(force);
}

void PackageSearch::onInventoryReady(bool success, const QString &error)
{
    if (m_pendingQuery == PendingQuery::None || m_pendingGeneration != m_generation)
        return;

    const PendingQuery query = m_pendingQuery;
    const QString value = m_pendingValue;
    m_pendingQuery = PendingQuery::None;
    m_pendingValue.clear();

    if (!success || !m_inventory->ready()) {
        clearResults();
        setSearching(false);
        const QString detail = error.isEmpty() ? m_inventory->errorString() : error;
        emit searchError(detail.isEmpty()
            ? tr("Classificazione dei pacchetti non disponibile.")
            : tr("Classificazione dei pacchetti non disponibile: %1").arg(detail));
        emit searchFinished();
        return;
    }

    switch (query) {
    case PendingQuery::Search:
        startRepoQuery(value);
        break;
    case PendingQuery::Installed:
        startListQuery(QStringLiteral("--installed"), true);
        break;
    case PendingQuery::Upgrades:
        startListQuery(QStringLiteral("--upgrades"), true);
        break;
    case PendingQuery::None:
        break;
    }
}

void PackageSearch::search(const QString &term)
{
    const QString sanitized = sanitizeTerm(term);
    ++m_generation;
    stopActiveProcess();
    m_pendingQuery = PendingQuery::None;

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
    requestInventory(PendingQuery::Search, sanitized, false);
}

void PackageSearch::loadInstalled(const QString &filter)
{
    static const QSet<QString> allowed = {
        QStringLiteral("all"), QStringLiteral("base"),
        QStringLiteral("persistent"), QStringLiteral("local")
    };
    const QString normalized = allowed.contains(filter) ? filter : QStringLiteral("all");
    const bool forceRefresh = m_loadedInstalledOnce && normalized == m_installedFilter;
    m_installedFilter = normalized;
    m_loadedInstalledOnce = true;

    ++m_generation;
    stopActiveProcess();
    m_pendingQuery = PendingQuery::None;
    clearResults();
    setSearching(true);
    requestInventory(PendingQuery::Installed, QString(), forceRefresh);
}

void PackageSearch::loadUpgrades()
{
    const bool forceRefresh = m_loadedUpgradesOnce;
    m_loadedUpgradesOnce = true;

    ++m_generation;
    stopActiveProcess();
    m_pendingQuery = PendingQuery::None;
    clearResults();
    setSearching(true);
    requestInventory(PendingQuery::Upgrades, QString(), forceRefresh);
}

void PackageSearch::releaseResults()
{
    ++m_generation;
    stopActiveProcess();
    m_pendingQuery = PendingQuery::None;
    m_pendingValue.clear();
    clearResults();
    m_results.squeeze();
    m_sourceResults.squeeze();
    setSearching(false);
    m_loadedUpgradesOnce = false;
}

void PackageSearch::startRepoQuery(const QString &term)
{
    const quint64 generation = m_generation;
    auto *rawProcess = new QProcess(this);
    const QPointer<QProcess> process(rawProcess);
    m_process = rawProcess;
    rawProcess->setProcessChannelMode(QProcess::SeparateChannels);

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

        const QSet<QString> &installed = m_inventory->installed();
        const QSet<QString> &owned = m_inventory->owned();
        const QSet<QString> &persistent = m_inventory->persistent();
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
            entry.installed = installed.contains(entry.name);
            entry.owned = owned.contains(entry.name);
            entry.persistent = persistent.contains(entry.name);
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

        const QSet<QString> &owned = m_inventory->owned();
        const QSet<QString> &persistent = m_inventory->persistent();
        QList<Entry> entries;
        for (const QVariant &value : parsed.values) {
            const QVariantMap row = value.toMap();
            Entry entry;
            entry.name = row.value(QStringLiteral("name")).toString();
            entry.arch = row.value(QStringLiteral("arch")).toString();
            entry.version = row.value(QStringLiteral("version")).toString();
            entry.repository = row.value(QStringLiteral("repository")).toString();
            entry.installed = installedEntries;
            entry.owned = owned.contains(entry.name);
            entry.persistent = persistent.contains(entry.name);

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
    if (m_results.isEmpty() && m_sourceResults.isEmpty())
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
