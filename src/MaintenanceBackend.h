#pragma once

#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QString>

class MaintenanceBackend : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool running READ running NOTIFY stateChanged)
    Q_PROPERTY(bool available READ available NOTIFY stateChanged)
    Q_PROPERTY(QString resultState READ resultState NOTIFY stateChanged)
    Q_PROPERTY(QString output READ output NOTIFY stateChanged)
    Q_PROPERTY(QString scope READ scope NOTIFY stateChanged)

public:
    explicit MaintenanceBackend(QObject *parent = nullptr);

    bool running() const { return m_running; }
    bool available() const;
    const QString &resultState() const { return m_resultState; }
    const QString &output() const { return m_output; }
    const QString &scope() const { return m_scope; }

    Q_INVOKABLE bool cleanTrash(const QString &scope);
    Q_INVOKABLE void cancel();

signals:
    void stateChanged();
    void finished(bool success, const QString &output);

private:
    void complete(const QString &state, const QString &output, bool success);

    QPointer<QProcess> m_process;
    bool m_running = false;
    bool m_cancelRequested = false;
    bool m_timedOut = false;
    QString m_resultState = QStringLiteral("idle");
    QString m_output;
    QString m_scope;
};
