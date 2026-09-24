#pragma once

#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QVariantList>
#include <QStringList>

class PolkitHelper;

class SoftwareBackend : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList repositories READ repositories NOTIFY repositoriesChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString errorText READ errorText NOTIFY errorTextChanged)
    Q_PROPERTY(bool operationRunning READ operationRunning NOTIFY operationStateChanged)
    Q_PROPERTY(QString operationState READ operationState NOTIFY operationStateChanged)
    Q_PROPERTY(QStringList operationLines READ operationLines NOTIFY operationStateChanged)
    Q_PROPERTY(bool canModifyRepositories READ canModifyRepositories NOTIFY operationStateChanged)

public:
    explicit SoftwareBackend(PolkitHelper *polkit, QObject *parent = nullptr);

    const QVariantList &repositories() const { return m_repositories; }
    bool busy() const { return m_busy; }
    const QString &errorText() const { return m_errorText; }
    bool operationRunning() const { return m_operationRunning; }
    const QString &operationState() const { return m_operationState; }
    const QStringList &operationLines() const { return m_operationLines; }
    bool canModifyRepositories() const;

    Q_INVOKABLE void refreshRepositories();
    Q_INVOKABLE bool enableRepository(const QString &repoId);
    Q_INVOKABLE bool disableRepository(const QString &repoId);
    Q_INVOKABLE bool addRepository(const QString &url);

signals:
    void repositoriesChanged();
    void busyChanged();
    void errorTextChanged();
    void operationStateChanged();
    void operationFinished(bool success, const QString &output);

private:
    void setBusy(bool busy);
    void setError(const QString &error);
    bool startPrivileged(const QStringList &args);

    PolkitHelper *m_polkit = nullptr;
    QVariantList m_repositories;
    QPointer<QProcess> m_process;
    bool m_busy = false;
    QString m_errorText;
    bool m_operationOwned = false;
    bool m_operationRunning = false;
    QString m_operationState = QStringLiteral("idle");
    QStringList m_operationLines;
};
