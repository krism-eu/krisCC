#pragma once

#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

class QTimer;
class QNetworkAccessManager;

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
    Q_PROPERTY(QVariantList grubEntries READ grubEntries NOTIFY bootEntriesChanged)
    Q_PROPERTY(bool bootEntriesBusy READ bootEntriesBusy NOTIFY bootEntriesChanged)
    Q_PROPERTY(QString bootEntriesError READ bootEntriesError NOTIFY bootEntriesChanged)
    Q_PROPERTY(int cpuUsagePercent READ cpuUsagePercent NOTIFY resourcesChanged)
    Q_PROPERTY(qint64 memoryUsedMiB READ memoryUsedMiB NOTIFY resourcesChanged)
    Q_PROPERTY(qint64 memoryTotalMiB READ memoryTotalMiB NOTIFY resourcesChanged)
    Q_PROPERTY(double cpuTemperatureC READ cpuTemperatureC NOTIFY resourcesChanged)
    Q_PROPERTY(QVariantMap serviceStates READ serviceStates NOTIFY serviceStatesChanged)
    Q_PROPERTY(QVariantList topMemoryProcesses READ topMemoryProcesses NOTIFY topMemoryProcessesChanged)
    Q_PROPERTY(QString networkInterface READ networkInterface NOTIFY networkChanged)
    Q_PROPERTY(QString networkAddress READ networkAddress NOTIFY networkChanged)
    Q_PROPERTY(QString networkState READ networkState NOTIFY networkChanged)
    Q_PROPERTY(QString networkKind READ networkKind NOTIFY networkChanged)
    Q_PROPERTY(bool backupBusy READ backupBusy NOTIFY backupBusyChanged)
    Q_PROPERTY(QString backupStatus READ backupStatus NOTIFY backupStatusChanged)
    Q_PROPERTY(QString backupPath READ backupPath NOTIFY backupStatusChanged)
    Q_PROPERTY(QString backupState READ backupState NOTIFY backupStatusChanged)
    Q_PROPERTY(QString backupDirectory READ backupDirectory NOTIFY backupDirectoryChanged)
    Q_PROPERTY(bool backupIsLocalSnapshot READ backupIsLocalSnapshot NOTIFY backupDirectoryChanged)
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
    const QVariantList &grubEntries() const { return m_grubEntries; }
    bool bootEntriesBusy() const { return m_bootEntriesBusy; }
    const QString &bootEntriesError() const { return m_bootEntriesError; }
    int cpuUsagePercent() const { return m_cpuUsagePercent; }
    qint64 memoryUsedMiB() const { return m_memoryUsedMiB; }
    qint64 memoryTotalMiB() const { return m_memoryTotalMiB; }
    double cpuTemperatureC() const { return m_cpuTemperatureC; }
    const QVariantMap &serviceStates() const { return m_serviceStates; }
    const QVariantList &topMemoryProcesses() const { return m_topMemoryProcesses; }
    const QString &networkInterface() const { return m_networkInterface; }
    const QString &networkAddress() const { return m_networkAddress; }
    const QString &networkState() const { return m_networkState; }
    const QString &networkKind() const { return m_networkKind; }

    bool backupBusy() const { return m_backupBusy; }
    const QString &backupStatus() const { return m_backupStatus; }
    const QString &backupPath() const { return m_backupPath; }
    const QString &backupState() const { return m_backupState; }
    const QString &backupDirectory() const { return m_backupDirectory; }
    bool backupIsLocalSnapshot() const;
    bool controlCenterUpdateBusy() const { return m_controlCenterUpdateBusy; }
    bool controlCenterUpdateAvailable() const { return m_controlCenterUpdateAvailable; }
    const QString &controlCenterLatestVersion() const { return m_controlCenterLatestVersion; }
    const QString &controlCenterUpdateStatus() const { return m_controlCenterUpdateStatus; }

    Q_INVOKABLE QString quickSystemInfo() const;
    Q_INVOKABLE void copyToClipboard(const QString &text) const;
    Q_INVOKABLE void refreshDashboardState();
    Q_INVOKABLE bool toolAvailable(const QString &toolId) const;
    Q_INVOKABLE bool launchTool(const QString &toolId) const;
    Q_INVOKABLE bool openTemporaryFolder() const;
    Q_INVOKABLE bool openHomeFolder() const;
    Q_INVOKABLE bool openRootFolder() const;
    Q_INVOKABLE bool programAvailable(const QString &program) const;
    Q_INVOKABLE void checkControlCenterUpdate();
    Q_INVOKABLE void refreshServiceStates();
    Q_INVOKABLE bool restartService(const QString &service);
    Q_INVOKABLE void requestReboot();
    Q_INVOKABLE void setResourceMonitoringEnabled(bool enabled);
    Q_INVOKABLE void refreshUefiEntries();
    Q_INVOKABLE void refreshGrubEntries();
    Q_INVOKABLE bool selectNextUefi(const QString &token);
    Q_INVOKABLE bool selectNextGrub(const QString &entry);
    Q_INVOKABLE void notify(const QString &summary, const QString &body = QString()) const;

    Q_INVOKABLE bool createSnapshot(const QString &kind);
    Q_INVOKABLE bool setBackupDirectory(const QString &pathOrUrl);
    Q_INVOKABLE bool cancelSnapshot();
    Q_INVOKABLE QVariantList backups() const;
    Q_INVOKABLE bool verifySnapshot(const QString &path);
    Q_INVOKABLE bool restoreSnapshot(const QString &path);
    Q_INVOKABLE bool deleteSnapshot(const QString &path);
    Q_INVOKABLE bool openBackupFolder() const;
    Q_INVOKABLE QVariantList backupPreview(const QString &kind) const;

    Q_INVOKABLE QString operationHistory() const;
    Q_INVOKABLE QVariantList operationHistoryEntries() const;
    Q_INVOKABLE bool clearOperationHistory();

signals:
    void backupBusyChanged();
    void backupStatusChanged();
    void backupDirectoryChanged();
    void rebootFinished(bool success, const QString &message);
    void bootSelectionStateChanged();
    void bootSelectionFinished(const QString &kind, bool success, const QString &output);
    void bootEntriesChanged();
    void resourcesChanged();
    void serviceStatesChanged();
    void storageSummaryChanged();
    void topMemoryProcessesChanged();
    void networkChanged();
    void controlCenterUpdateChanged();

private:
    QString readOsName() const;
    QString toolProgram(const QString &toolId) const;
    QString resolveExecutable(const QString &program) const;
    bool validateBackupPath(const QString &path, QString *canonicalPath = nullptr) const;
    QString defaultBackupDirectory() const;
    bool validateBackupDirectory(const QString &path, QString *canonicalPath = nullptr) const;
    QString currentBackupRoot() const;
    void setBackupBusy(bool busy);
    void setBackupResult(const QString &status, const QString &path = QString(),
                         const QString &state = QStringLiteral("idle"));
    void refreshResources();
    void refreshTopMemoryProcesses();
    void refreshNetworkState();
    double readCpuTemperature() const;

    PolkitHelper *m_polkit = nullptr;
    QNetworkAccessManager *m_networkAccess = nullptr;
    bool m_bootSelectionOwned = false;
    bool m_bootSelectionRunning = false;
    QString m_bootSelectionKind;
    QString m_bootSelectionState = QStringLiteral("idle");
    QVariantList m_uefiEntries;
    QVariantList m_grubEntries;
    QPointer<QProcess> m_bootEntriesProcess;
    bool m_bootEntriesBusy = false;
    QString m_bootEntriesError;
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
    QString m_networkAddress;
    QString m_networkState = QStringLiteral("down");
    QString m_networkKind = QStringLiteral("ethernet");
    quint64 m_serviceRefreshGeneration = 0;
    bool m_controlCenterUpdateBusy = false;
    bool m_controlCenterUpdateAvailable = false;
    QString m_controlCenterLatestVersion;
    QString m_controlCenterUpdateStatus;
    QPointer<ProcessRunner> m_backupRunner;
    bool m_backupBusy = false;
    QString m_backupStatus;
    QString m_backupPath;
    QString m_backupState = QStringLiteral("idle");
    QString m_backupDirectory;
    QString m_backupPartialPath;
};