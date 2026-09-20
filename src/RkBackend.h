#pragma once

#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QString>
#include <QStringList>

class PolkitHelper;

class RkBackend : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(bool statusValid READ statusValid NOTIFY stateChanged)
    Q_PROPERTY(QString overlayState READ overlayState NOTIFY stateChanged)
    Q_PROPERTY(bool pendingRecovery READ pendingRecovery NOTIFY stateChanged)
    Q_PROPERTY(bool needsSync READ needsSync NOTIFY stateChanged)
    Q_PROPERTY(QStringList requests READ requests NOTIFY stateChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY stateChanged)
    Q_PROPERTY(QString errorText READ errorText NOTIFY stateChanged)
    Q_PROPERTY(bool operationRunning READ operationRunning NOTIFY operationStateChanged)
    Q_PROPERTY(QString operationState READ operationState NOTIFY operationStateChanged)
    Q_PROPERTY(QString operationOutput READ operationOutput NOTIFY operationStateChanged)
    Q_PROPERTY(bool canSync READ canSync NOTIFY stateChanged)
    Q_PROPERTY(bool canForget READ canForget NOTIFY stateChanged)

public:
    explicit RkBackend(PolkitHelper *polkit, QObject *parent = nullptr);

    bool busy() const { return m_busy; }
    bool statusValid() const { return m_statusValid; }
    const QString &overlayState() const { return m_overlayState; }
    bool pendingRecovery() const { return m_pendingRecovery; }
    bool needsSync() const { return m_needsSync; }
    const QStringList &requests() const { return m_requests; }
    const QString &statusText() const { return m_statusText; }
    const QString &errorText() const { return m_errorText; }
    bool operationRunning() const { return m_operationRunning; }
    const QString &operationState() const { return m_operationState; }
    const QString &operationOutput() const { return m_operationOutput; }
    bool canSync() const;
    bool canForget() const;

    Q_INVOKABLE void refreshStatus();
    Q_INVOKABLE bool sync();
    Q_INVOKABLE bool forget(const QString &packageName);

signals:
    void stateChanged();
    void operationStateChanged();
    void operationFinished(bool success, const QString &output);

private:
    void parseStatus(const QString &text);
    void finishStatusError(const QString &message);
    bool validPackageName(const QString &packageName) const;
    void startPrivileged(const QStringList &args);

    PolkitHelper *m_polkit = nullptr;
    QPointer<QProcess> m_process;
    bool m_busy = false;
    bool m_statusValid = false;
    bool m_pendingRecovery = false;
    bool m_needsSync = false;
    bool m_operationOwned = false;
    bool m_operationRunning = false;
    QString m_overlayState = QStringLiteral("unknown");
    QStringList m_requests;
    QString m_statusText;
    QString m_errorText;
    QString m_operationState = QStringLiteral("idle");
    QString m_operationOutput;
};
