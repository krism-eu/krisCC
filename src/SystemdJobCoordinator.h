#pragma once

#include <QDBusObjectPath>
#include <QHash>
#include <QObject>
#include <QTimer>

class QDBusServiceWatcher;

class SystemdJobCoordinator final : public QObject
{
    Q_OBJECT
public:
    explicit SystemdJobCoordinator(QObject *parent = nullptr);
    ~SystemdJobCoordinator() override;

    bool start(const QString &unit, const QString &action);
    bool busy() const { return m_active; }

signals:
    void completed(const QString &unit, const QString &action,
                   bool success, const QString &message);
    void stateMayHaveChanged();

private slots:
    void onJobRemoved(uint id, const QDBusObjectPath &job,
                      const QString &unit, const QString &result);
    void onOwnerChanged(const QString &service,
                        const QString &oldOwner, const QString &newOwner);

private:
    void ensureSubscribedAndSend();
    void sendRequest();
    void acceptJobPath(const QString &path);
    void finish(bool success, const QString &message);
    QString currentOwner() const;

    QDBusServiceWatcher *m_serviceWatcher = nullptr;
    QTimer m_timeout;
    QHash<QString, QString> m_earlyResults;
    QString m_owner;
    QString m_unit;
    QString m_action;
    QString m_jobPath;
    bool m_subscribed = false;
    bool m_active = false;
};
