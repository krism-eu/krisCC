#pragma once

#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QString>
#include <QVariantList>

class SystemBackend : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString osName READ osName CONSTANT)
    Q_PROPERTY(QString kernelVersion READ kernelVersion CONSTANT)
    Q_PROPERTY(QString architecture READ architecture CONSTANT)
    Q_PROPERTY(QString hostName READ hostName CONSTANT)
    Q_PROPERTY(QString memorySummary READ memorySummary CONSTANT)
    Q_PROPERTY(QString storageSummary READ storageSummary CONSTANT)
    Q_PROPERTY(QString desktopSession READ desktopSession CONSTANT)
    Q_PROPERTY(QString selinuxState READ selinuxState CONSTANT)
    Q_PROPERTY(bool backupBusy READ backupBusy NOTIFY backupBusyChanged)
    Q_PROPERTY(QString backupStatus READ backupStatus NOTIFY backupStatusChanged)
    Q_PROPERTY(QString backupPath READ backupPath NOTIFY backupStatusChanged)
    Q_PROPERTY(QString backupState READ backupState NOTIFY backupStatusChanged)

public:
    explicit SystemBackend(QObject *parent = nullptr);
    ~SystemBackend() override;

    QString osName() const;
    QString kernelVersion() const;
    QString architecture() const;
    QString hostName() const;
    QString memorySummary() const;
    QString storageSummary() const;
    QString desktopSession() const;
    QString selinuxState() const;

    bool backupBusy() const { return m_backupBusy; }
    const QString &backupStatus() const { return m_backupStatus; }
    const QString &backupPath() const { return m_backupPath; }
    const QString &backupState() const { return m_backupState; }

    Q_INVOKABLE QString quickSystemInfo() const;
    Q_INVOKABLE void copyToClipboard(const QString &text) const;
    Q_INVOKABLE QString flatpakIconPath(const QString &appId) const;
    Q_INVOKABLE bool toolAvailable(const QString &toolId) const;
    Q_INVOKABLE bool launchTool(const QString &toolId) const;
    Q_INVOKABLE bool programAvailable(const QString &program) const;
    Q_INVOKABLE QString serviceState(const QString &service) const;
    Q_INVOKABLE bool restartService(const QString &service);
    Q_INVOKABLE void requestReboot();
    Q_INVOKABLE void notify(const QString &summary, const QString &body = QString()) const;

    Q_INVOKABLE bool createSnapshot(const QString &kind);
    Q_INVOKABLE bool cancelSnapshot();
    Q_INVOKABLE QVariantList backups() const;
    Q_INVOKABLE bool verifySnapshot(const QString &path);
    Q_INVOKABLE bool restoreSnapshot(const QString &path);
    Q_INVOKABLE bool openBackupFolder() const;
    Q_INVOKABLE QVariantList backupPreview(const QString &kind) const;

    Q_INVOKABLE QString operationHistory() const;
    Q_INVOKABLE QVariantList operationHistoryEntries() const;
    Q_INVOKABLE bool clearOperationHistory();

signals:
    void backupBusyChanged();
    void backupStatusChanged();
    void rebootFinished(bool success, const QString &message);

private:
    QString readOsName() const;
    QString toolProgram(const QString &toolId) const;
    QString resolveExecutable(const QString &program) const;
    bool validateBackupPath(const QString &path, QString *canonicalPath = nullptr) const;
    void setBackupBusy(bool busy);
    void setBackupResult(const QString &status, const QString &path = QString(),
                         const QString &state = QStringLiteral("idle"));

    QPointer<QProcess> m_backupProcess;
    bool m_backupBusy = false;
    QString m_backupStatus;
    QString m_backupPath;
    QString m_backupState = QStringLiteral("idle");
    QString m_backupPartialPath;
    bool m_backupCancelled = false;
};
