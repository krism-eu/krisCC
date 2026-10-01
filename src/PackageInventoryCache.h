#pragma once

#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QSet>
#include <QString>

class PackageInventoryCache final : public QObject
{
    Q_OBJECT
public:
    struct Config {
        QString rpmProgram = QStringLiteral("/usr/bin/rpm");
        QString ownedManifest = QStringLiteral("/usr/share/krisos/owned-packages.txt");
        QString persistentManifest = QStringLiteral("/var/lib/krisos/packages.list");
        int ttlMs = 60 * 1000;
        int timeoutMs = 30 * 1000;
    };

    explicit PackageInventoryCache(QObject *parent = nullptr);
    explicit PackageInventoryCache(const Config &config, QObject *parent = nullptr);
    ~PackageInventoryCache() override;

    static PackageInventoryCache *shared();

    void ensureFresh(bool force = false);
    void invalidate();

    bool ready() const { return m_ready && !m_loading; }
    bool loading() const { return m_loading; }
    QString errorString() const { return m_error; }
    const QSet<QString> &installed() const { return m_installed; }
    const QSet<QString> &owned() const { return m_owned; }
    const QSet<QString> &persistent() const { return m_persistent; }
    quint64 loadCount() const { return m_loadCount; }

signals:
    void refreshFinished(bool success, const QString &error);
    void inventoryChanged();

private:
    bool refreshMetadata(QSet<QString> *owned, QSet<QString> *persistent, QString *error) const;
    bool readManifest(const QString &path, bool missingIsEmpty, QSet<QString> *target,
                      QString *error) const;
    void startInstalledRefresh(QSet<QString> owned, QSet<QString> persistent,
                               quint64 requestEpoch);
    void finishRefresh(bool success, const QString &error = QString());
    bool expired() const;

    Config m_config;
    QPointer<QProcess> m_process;
    QSet<QString> m_installed;
    QSet<QString> m_owned;
    QSet<QString> m_persistent;
    bool m_ready = false;
    bool m_loading = false;
    bool m_forceAgain = false;
    QString m_error;
    qint64 m_lastSuccessMs = 0;
    quint64 m_epoch = 0;
    quint64 m_loadCount = 0;
};
