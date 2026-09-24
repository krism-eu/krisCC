#pragma once

#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantList>

class ProcessRunner;

class CustomActionsBackend : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList actions READ actions NOTIFY actionsChanged)
    Q_PROPERTY(bool running READ running NOTIFY stateChanged)
    Q_PROPERTY(QString runningId READ runningId NOTIFY stateChanged)
    Q_PROPERTY(QString output READ output NOTIFY stateChanged)
    Q_PROPERTY(QString resultState READ resultState NOTIFY stateChanged)
    Q_PROPERTY(QString errorText READ errorText NOTIFY stateChanged)

public:
    explicit CustomActionsBackend(QObject *parent = nullptr);
    ~CustomActionsBackend() override;

    const QVariantList &actions() const { return m_actions; }
    bool running() const { return m_running; }
    const QString &runningId() const { return m_runningId; }
    const QString &output() const { return m_output; }
    const QString &resultState() const { return m_resultState; }
    const QString &errorText() const { return m_errorText; }

    Q_INVOKABLE void reload();
    Q_INVOKABLE bool saveAction(const QString &id, const QString &name,
                                const QString &description, const QString &script,
                                bool confirmBeforeRun);
    Q_INVOKABLE bool removeAction(const QString &id);
    Q_INVOKABLE bool runAction(const QString &id);
    Q_INVOKABLE void cancel();

signals:
    void actionsChanged();
    void stateChanged();

private:
    QString storagePath() const;
    bool persist();
    bool validateAction(const QString &name, const QString &description,
                        const QString &script, QString *error) const;
    bool validId(const QString &id) const;
    int indexForId(const QString &id) const;
    void appendOutput(const QByteArray &data);
    void finish(const QString &state, const QString &message = QString());

    QVariantList m_actions;
    QPointer<ProcessRunner> m_runner;
    bool m_storageValid = true;
    bool m_running = false;
    QString m_runningId;
    QString m_output;
    QString m_resultState = QStringLiteral("idle");
    QString m_errorText;
};
