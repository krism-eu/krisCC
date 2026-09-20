#pragma once

#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QVariantList>

class PolkitHelper;

class BootcBackend : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool bootcAvailable READ bootcAvailable CONSTANT)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged)
    Q_PROPERTY(QString errorText READ errorText NOTIFY statusChanged)
    Q_PROPERTY(QVariantList deployments READ deployments NOTIFY statusChanged)
    Q_PROPERTY(QString persistentPackages READ persistentPackages NOTIFY packagesChanged)
    Q_PROPERTY(int persistentPackageCount READ persistentPackageCount NOTIFY packagesChanged)
    Q_PROPERTY(bool operationRunning READ operationRunning NOTIFY operationStateChanged)
    Q_PROPERTY(QString operationState READ operationState NOTIFY operationStateChanged)
    Q_PROPERTY(QStringList operationLines READ operationLines NOTIFY operationStateChanged)
    Q_PROPERTY(bool canOperate READ canOperate NOTIFY operationStateChanged)

public:
    explicit BootcBackend(PolkitHelper *polkit, QObject *parent = nullptr);

    bool bootcAvailable() const;
    bool busy() const { return m_busy; }
    const QString &statusText() const { return m_statusText; }
    const QString &errorText() const { return m_errorText; }
    const QVariantList &deployments() const { return m_deployments; }
    const QString &persistentPackages() const { return m_persistentPackages; }
    int persistentPackageCount() const { return m_persistentPackageCount; }
    bool operationRunning() const { return m_operationRunning; }
    const QString &operationState() const { return m_operationState; }
    const QStringList &operationLines() const { return m_operationLines; }
    bool canOperate() const;

    Q_INVOKABLE void refreshStatus();
    Q_INVOKABLE void refreshPackages();
    Q_INVOKABLE bool checkUpgrade();
    Q_INVOKABLE bool downloadUpgrade();
    Q_INVOKABLE bool prepareUpgrade();
    Q_INVOKABLE bool applyDownloaded();

signals:
    void busyChanged();
    void statusChanged();
    void packagesChanged();
    void operationStateChanged();
    void operationFinished(bool success, const QString &output);

private:
    void setBusy(bool busy);
    void loadPackages();
    void startHumanStatus(const QString &previousError = QString());
    void parseJsonStatus(const QByteArray &data);
    bool startPrivileged(const QStringList &args);

    PolkitHelper *m_polkit = nullptr;
    QPointer<QProcess> m_process;
    bool m_busy = false;
    QString m_statusText;
    QString m_errorText;
    QVariantList m_deployments;
    QString m_persistentPackages;
    int m_persistentPackageCount = 0;
    bool m_operationOwned = false;
    bool m_operationRunning = false;
    QString m_operationState = QStringLiteral("idle");
    QStringList m_operationLines;
};
