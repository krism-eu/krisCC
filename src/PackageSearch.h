#pragma once

#include <QAbstractListModel>
#include <QPointer>
#include <QProcess>
#include <QSet>
#include <QString>

class PackageInventoryCache;

class PackageSearch : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(bool searching READ searching NOTIFY searchingChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool truncated READ truncated NOTIFY truncatedChanged)

public:
    enum Roles {
        NameRole = Qt::UserRole + 1,
        SummaryRole,
        InstalledRole,
        OwnedRole,
        PersistentRole,
        VersionRole,
        RepositoryRole,
        ArchRole,
        DownloadSizeRole,
        InstallSizeRole
    };

    explicit PackageSearch(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void search(const QString &term);
    Q_INVOKABLE void loadInstalled(const QString &filter = QString());
    Q_INVOKABLE void loadUpgrades();
    Q_INVOKABLE void setLocalFilter(const QString &text);
    Q_INVOKABLE void refreshInventory();
    static void invalidateSharedInventory();

    bool searching() const { return m_searching; }
    int count() const { return m_results.size(); }
    bool truncated() const { return m_truncated; }

signals:
    void searchingChanged();
    void countChanged();
    void truncatedChanged();
    void searchFinished();
    void searchError(const QString &message);

private:
    enum class PendingQuery {
        None,
        Search,
        Installed,
        Upgrades
    };

    struct Entry {
        QString name;
        QString summary;
        QString version;
        QString repository;
        QString arch;
        quint64 downloadSize = 0;
        quint64 installSize = 0;
        bool installed = false;
        bool owned = false;
        bool persistent = false;
    };

    void clearResults();
    void setSearching(bool searching);
    void requestInventory(PendingQuery query, const QString &value, bool force);
    void onInventoryReady(bool success, const QString &error);
    void startRepoQuery(const QString &term);
    void startListQuery(const QString &filter, bool installedEntries);
    void stopActiveProcess();
    void applyLocalFilter();
    static QString sanitizeTerm(const QString &term);

    QList<Entry> m_results;
    QList<Entry> m_sourceResults;
    QPointer<QProcess> m_process;
    PackageInventoryCache *m_inventory = nullptr;
    PendingQuery m_pendingQuery = PendingQuery::None;
    QString m_pendingValue;
    QString m_installedFilter = QStringLiteral("all");
    QString m_localFilter;
    bool m_searching = false;
    bool m_truncated = false;
    bool m_loadedInstalledOnce = false;
    bool m_loadedUpgradesOnce = false;
    quint64 m_generation = 0;
    quint64 m_pendingGeneration = 0;
};
