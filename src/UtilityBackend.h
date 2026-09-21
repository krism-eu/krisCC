#pragma once

#include <QObject>
#include <QPointer>
#include <QString>

class ProcessRunner;

class UtilityBackend : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(QString title READ title NOTIFY stateChanged)
    Q_PROPERTY(QString output READ output NOTIFY stateChanged)
    Q_PROPERTY(QString operationId READ operationId NOTIFY stateChanged)
    Q_PROPERTY(QString resultState READ resultState NOTIFY stateChanged)
    Q_PROPERTY(QVariantList rows READ rows NOTIFY stateChanged)

public:
    explicit UtilityBackend(QObject *parent = nullptr);

    bool busy() const { return m_busy; }
    const QString &title() const { return m_title; }
    const QString &output() const { return m_output; }
    const QString &operationId() const { return m_operationId; }
    const QString &resultState() const { return m_resultState; }
    const QVariantList &rows() const { return m_rows; }

    Q_INVOKABLE bool runBookmark(const QString &id);
    Q_INVOKABLE bool previewRpmInstall(const QString &packageName);
    Q_INVOKABLE bool runFlatpak(const QString &mode, const QString &query = QString(),
                                 const QString &remote = QString());
    Q_INVOKABLE bool addFlathubUser();
    Q_INVOKABLE bool runPodman(const QString &mode, const QString &container = QString(), const QString &value = QString());
    Q_INVOKABLE bool cancel();
    Q_INVOKABLE void clearResult();

signals:
    void stateChanged();

private:
    bool start(const QString &program, const QStringList &args, const QString &title,
               const QString &operationId, int timeoutMs = 0);
    bool validPackageName(const QString &name) const;
    bool validContainerName(const QString &name) const;
    void finish(const QString &message, const QString &state);
    void setImmediateError(const QString &title, const QString &operationId, const QString &message);

    QPointer<ProcessRunner> m_runner;
    bool m_busy = false;
    QString m_title;
    QString m_output;
    QString m_operationId;
    QString m_resultState = QStringLiteral("idle");
    QVariantList m_rows;
};