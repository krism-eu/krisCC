#pragma once

#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

class QTimer;
class QNetworkAccessManager;
class QNetworkReply;

class PolkitHelper;
class ProcessRunner;

class SystemBackend : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString osName READ osName CONSTANT)
    Q_PROPERTY(QString kernelVersion READ kernelVersion CONSTANT)
    Q_PROPERTY(QString architecture READ architecture CONSTANT)
    Q_PROPERTY(QString hostName READ hostName CONSTANT)
    Q_PROPERTY(QString memorySummary READ memorySummary CONSTANT)
    Q_PROPERTY(QString storageSummary READ storageSummary NOTIFY storageSummaryChanged)
    Q_PROPERTY(QString desktopSession READ desktopSession CONSTANT)
    Q_PROPERTY(QString selinuxState READ selinuxState CONSTANT)
    Q_PROPERTY(bool bootSelectionRunning READ bootSelectionRunning NOTIFY bootSelectionStateChanged)
    Q_PROPERTY(QString bootSelectionState READ bootSelectionState NOTIFY bootSelectionStateChanged)
    Q_PROPERTY(bool canSelectNextBoot READ canSelectNextBoot NOTIFY bootSelectionStateChanged)
    Q_PROPERTY(bool uefiBootAvailable READ uefiBootAvailable CONSTANT)
    Q_PROPERTY(bool grubEntriesAvailable READ grubEntriesAvailable CONSTANT)
    Q_PROPERTY(bool grubNextBootAvailable READ grubNextBootAvailable CONSTANT)
    Q_PROPERTY(QVariantList uefiEntries READ uefiEntries NOTIFY bootEntriesChanged)
    Q_PROPERTY(QString nextUefiBootLabel READ nextUefiBootLabel NOTIFY bootEntriesChanged)
    Q_PROPERTY(QString currentUefiBootCode READ currentUefiBootCode NOTIFY bootEntriesChanged)
    Q_PROPERTY(QStringList uefiBootOrder READ uefiBootOrder NOTIFY bootEntriesChanged)
    Q_PROPERTY(QVariantList grubEntries READ grubEntries NOTIFY bootEntriesChanged)
    Q_PROPERTY(int cpuUsagePercent READ cpuUsagePercent NOTIFY resourcesChanged)
    Q_PROPERTY(qint64 memoryUsedMiB READ memoryUsedMiB NOTIFY resourcesChanged)
    Q_PROPERTY(qint64 memoryTotalMiB READ memoryTotalMiB NOTIFY resourcesChanged)
    Q_PROPERTY(double cpuTemperatureC READ cpuTemperatureC NOTIFY resourcesChanged)
    Q_PROPERTY(QVariantMap serviceStates READ serviceStates NOTIFY serviceStatesChanged)
    Q_PROPERTY(QVariantList topMemoryProcesses READ topMemoryProcesses NOTIFY topMemoryProcessesChanged)
    Q_PROPERTY(QString networkInterface READ networkInterface NOTIFY networkChanged)
    Q_PROPERTY(QString networkDisplayName READ networkDisplayName NOTIFY networkChanged)
    Q_PROPERTY(QString networkAddress READ networkAddress NOTIFY networkChanged)
    Q_PROPERTY(QString networkState READ networkState NOTIFY networkChanged)
    Q_PROPERTY(QString networkKind READ networkKind NOTIFY networkChanged)
    Q_PROPERTY(bool internetIdentityBusy READ internetIdentityBusy NOTIFY internetIdentityChanged)
    Q_PROPERTY(QString internetIdentity READ internetIdentity NOTIFY internetIdentityChanged)
    Q_PROPERTY(bool controlCenterUpdateBusy READ controlCenterUpdateBusy NOTIFY controlCenterUpdateChanged)
    Q_PROPERTY(bool controlCenterUpdateAvailable READ controlCenterUpdateAvailable NOTIFY controlCenterUpdateChanged)
    Q_PROPERTY(QString controlCenterLatestVersion READ controlCenterLatestVersion NOTIFY controlCenterUpdateChanged)
    Q_PROPERTY(QString controlCenterUpdateStatus READ controlCenterUpdateStatus NOTIFY controlCenterUpdateChanged)

public:
    explicit SystemBackend(PolkitHelper *polkit, QObject *parent = nullptr);
    ~SystemBackend() override;

    QString osName() const;
    QString kernelVersion() const;
    QString architecture() const;
    QString hostName() const;
    QString memorySummary() const;
    QString storageSummary() const;
    QString desktopSession() const;
    QString selinuxState() const;
    bool bootSelectionRunning() const { return m_bootSelectionRunning; }
    const QString &bootSelectionState() const { return m_bootSelectionState; }
    bool canSelectNextBoot() const;
    bool uefiBootAvailable() const;
    bool grubEntriesAvailable() const;
    bool grubNextBootAvailable() const;
    const QVariantList &uefiEntries() const { return m_uefiEntries; }
    const QString &nextUefiBootLabel() const { return m_nextUefiBootLabel; }
    const QString &currentUefiBootCode() const { return m_currentUefiBootCode; }
    const QStringList &uefiBootOrder() const { return m_uefiBootOrder; }
    const QVariantList &grubEntries() const { return m_grubEntries; }
    int cpuUsagePercent() const { return m_cpuUsagePercent; }
    qint64 memoryUsedMiB() const { return m_memoryUsedMiB; }
    qint64 memoryTotalMiB() const { return m_memoryTotalMiB; }
    double cpuTemperatureC() const { return m_cpuTemperatureC; }
    const QVariantMap &serviceStates() const { return m_serviceStates; }
    const QVariantList &topMemoryProcesses() const { return m_topMemoryProcesses; }
    const QString &networkInterface() const { return m_networkInterface; }
    const QString &networkDisplayName() const { return m_networkDisplayName; }
    const QString &networkAddress() const { return m_networkAddress; }
    const QString &networkState() const { return m_networkState; }
    const QString &networkKind() const { return m_networkKind; }
    bool internetIdentityBusy() const { return m_internetIdentityBusy; }
    const QString &internetIdentity() const { return m_internetIdentity; }

    bool controlCenterUpdateBusy() const { return m_controlCenterUpdateBusy; }
    bool controlCenterUpdateAvailable() const { return m_controlCenterUpdateAvailable; }
    const QString &controlCenterLatestVersion() const { return m_controlCenterLatestVersion; }
    const QString &controlCenterUpdateStatus() const { return m_controlCenterUpdateStatus; }

    Q_INVOKABLE QString quickSystemInfo() const;
    Q_INVOKABLE QString saveSupportReport(const QString &text) const;
    Q_INVOKABLE void copyToClipboard(const QString &text) const;
    Q_INVOKABLE void refreshDashboardState();
    Q_INVOKABLE bool toolAvailable(const QString &toolId) const;
    Q_INVOKABLE bool launchTool(const QString &toolId) const;
    Q_INVOKABLE bool openTemporaryFolder() const;
    Q_INVOKABLE bool openHomeFolder() const;
    Q_INVOKABLE bool openRootFolder() const;
    Q_INVOKABLE bool programAvailable(const QString &program) const;
    Q_INVOKABLE void checkControlCenterUpdate();
    // Implemented only by the runtime; keep Qt invocations on the virtual interface.
    Q_INVOKABLE virtual void refreshServiceStates() = 0;
    Q_INVOKABLE virtual bool startService(const QString &service) = 0;
    Q_INVOKABLE virtual bool stopService(const QString &service) = 0;
    Q_INVOKABLE virtual bool restartService(const QString &service) = 0;
    Q_INVOKABLE bool resetFailedService(const QString &service);
    Q_INVOKABLE virtual void requestReboot();
    Q_INVOKABLE virtual void requestFirmwareReboot();
    Q_INVOKABLE QStringList kernelArguments() const;
    Q_INVOKABLE bool vacuumJournal();
    Q_INVOKABLE bool cleanDnfCache();
    Q_INVOKABLE bool openNetworkSettings() const;
    Q_INVOKABLE void checkInternetIdentity();
    Q_INVOKABLE void setResourceMonitoringEnabled(bool enabled);
    Q_INVOKABLE virtual void refreshUefiEntries() = 0;
    Q_INVOKABLE virtual void refreshUefiEntriesPrivileged() = 0;
    Q_INVOKABLE virtual void refreshGrubEntries() = 0;
    Q_INVOKABLE bool selectNextUefi(const QString &token);
    Q_INVOKABLE bool clearNextUefi();
    Q_INVOKABLE bool deleteUefiEntry(const QString &token);
    Q_INVOKABLE bool moveUefiEntry(const QString &token, int direction);
    Q_INVOKABLE bool selectNextGrub(const QString &entry);
    Q_INVOKABLE void notify(const QString &summary, const QString &body = QString()) const;

    Q_INVOKABLE QVariantList operationHistoryEntries() const;
    Q_INVOKABLE bool clearOperationHistory();

signals:
    void rebootFinished(bool success, const QString &message);
    void bootSelectionStateChanged();
    void bootSelectionFinished(const QString &kind, bool success, const QString &output);
    void bootEntriesChanged();
    void resourcesChanged();
    void serviceStatesChanged();
    void storageSummaryChanged();
    void topMemoryProcessesChanged();
    void networkChanged();
    void internetIdentityChanged();
    void controlCenterUpdateChanged();
    void adminMaintenanceFinished(const QString &operation, bool success, const QString &output);

protected:
    bool setWifiRadio(bool enabled, bool restartAfter);
    QString readOsName() const;
    QString toolProgram(const QString &toolId) const;
    QString resolveExecutable(const QString &program) const;
    void refreshResources();
    void refreshTopMemoryProcesses();
    void refreshNetworkState();
    double readCpuTemperature() const;
    bool applyUefiBootOrder(const QStringList &order);
    void applyUefiEntriesOutput(const QString &output);

    PolkitHelper *m_polkit = nullptr;
    QNetworkAccessManager *m_networkAccess = nullptr;
    bool m_bootSelectionOwned = false;
    bool m_adminMaintenanceOwned = false;
    QString m_adminMaintenanceOperation;
    bool m_bootSelectionRunning = false;
    QString m_bootSelectionKind;
    QString m_bootSelectionState = QStringLiteral("idle");
    QVariantList m_uefiEntries;
    QString m_nextUefiBootLabel;
    QString m_currentUefiBootCode;
    QStringList m_uefiBootOrder;
    QVariantList m_grubEntries;
    QTimer *m_resourceTimer = nullptr;
    bool m_resourceMonitoringEnabled = false;
    quint64 m_previousCpuTotal = 0;
    quint64 m_previousCpuIdle = 0;
    int m_cpuUsagePercent = -1;
    qint64 m_memoryUsedMiB = -1;
    qint64 m_memoryTotalMiB = -1;
    double m_cpuTemperatureC = -1.0;
    QVariantMap m_serviceStates;
    QVariantList m_topMemoryProcesses;
    qint64 m_lastTopMemoryRefreshMs = 0;
    QString m_networkInterface;
    QString m_networkDisplayName;
    QString m_networkAddress;
    QString m_networkState = QStringLiteral("down");
    QString m_networkKind = QStringLiteral("ethernet");
    bool m_internetIdentityBusy = false;
    QString m_internetIdentity;
    QPointer<QNetworkReply> m_internetIdentityReply;
    quint64 m_serviceRefreshGeneration = 0;
    bool m_controlCenterUpdateBusy = false;
    bool m_controlCenterUpdateAvailable = false;
    QString m_controlCenterLatestVersion;
    QString m_controlCenterUpdateStatus;
};
