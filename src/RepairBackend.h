#pragma once
#include <QObject>
#include <QPointer>
#include <QString>
class ProcessRunner;
class RepairBackend : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    Q_PROPERTY(QString output READ output NOTIFY stateChanged)
public:
    explicit RepairBackend(QObject *parent=nullptr);
    bool busy() const { return m_busy; }
    const QString &state() const { return m_state; }
    const QString &output() const { return m_output; }
    Q_INVOKABLE bool restartAudio();
    Q_INVOKABLE bool flushDns();
    Q_INVOKABLE bool reconnectNetwork(const QString &interfaceName);
    Q_INVOKABLE bool cancel();
signals: void stateChanged();
private:
    bool start(const QString &program, const QStringList &args, const QString &operation);
    bool fail(const QString &operation, const QString &message);
    QPointer<ProcessRunner> m_runner;
    bool m_busy=false;
    QString m_state=QStringLiteral("idle");
    QString m_output;
    QString m_operation;
};
