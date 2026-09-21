#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QPointer>
#include <QSet>
#include <QString>

class ProcessRunner;

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
    Q_INVOKABLE void loadRecent();
    Q_INVOKABLE void setLocalFilter(const QString &text);
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
    void startInstalledQuery(const QString &term);
    void startRepoQuery(const QString &term);
    void startListQuery(const QString &filter, bool installedEntries);
    void stopActiveProcess();
    void refreshPersistentSet();
    void applyLocalFilter();
    static QString sanitizeTerm(const QString &term);

    QList<Entry> m_results;
    QList<Entry> m_sourceResults;
    QSet<QString> m_owned;
    QSet<QString> m_installed;
    QSet<QString> m_persistent;
    QPointer<ProcessRunner> m_runner;
    QString m_installedFilter = QStringLiteral("all");
    QString m_localFilter;
    bool m_searching = false;
    bool m_truncated = false;
    quint64 m_generation = 0;
};