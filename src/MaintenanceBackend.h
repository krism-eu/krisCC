#pragma once

#include <QObject>
#include <QString>

class PolkitHelper;

class MaintenanceBackend : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool running READ running NOTIFY stateChanged)
    Q_PROPERTY(bool available READ available NOTIFY stateChanged)
    Q_PROPERTY(QString resultState READ resultState NOTIFY stateChanged)
    Q_PROPERTY(QString output READ output NOTIFY stateChanged)
    Q_PROPERTY(QString scope READ scope NOTIFY stateChanged)

public:
    explicit MaintenanceBackend(PolkitHelper *polkit, QObject *parent = nullptr);

    bool running() const { return m_running; }
    bool available() const;
    const QString &resultState() const { return m_resultState; }
    const QString &output() const { return m_output; }
    const QString &scope() const { return m_scope; }

    Q_INVOKABLE bool cleanTrash(const QString &scope);

signals:
    void stateChanged();
    void finished(bool success, const QString &output);

private:
    PolkitHelper *m_polkit = nullptr;
    bool m_ownedOperation = false;
    bool m_running = false;
    QString m_resultState = QStringLiteral("idle");
    QString m_output;
    QString m_scope;
};
