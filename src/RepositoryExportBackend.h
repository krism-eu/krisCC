#pragma once

#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <memory>

class ProcessRunner;
class QNetworkReply;
class QSaveFile;
class QTemporaryDir;

class RepositoryExportBackend final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString repository READ repository NOTIFY stateChanged)
    Q_PROPERTY(QStringList branches READ branches NOTIFY branchesChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY stateChanged)
    Q_PROPERTY(QString errorText READ errorText NOTIFY stateChanged)
    Q_PROPERTY(QString outputPath READ outputPath NOTIFY stateChanged)

public:
    explicit RepositoryExportBackend(QObject *parent = nullptr);
    ~RepositoryExportBackend() override;

    const QString &repository() const { return m_repository; }
    const QStringList &branches() const { return m_branches; }
    bool busy() const { return m_busy; }
    const QString &statusText() const { return m_statusText; }
    const QString &errorText() const { return m_errorText; }
    const QString &outputPath() const { return m_outputPath; }

    Q_INVOKABLE bool refreshBranches(const QString &repository);
    Q_INVOKABLE bool exportBranch(const QString &repository, const QString &branch);
    Q_INVOKABLE void cancel();

signals:
    void branchesChanged();
    void stateChanged();

private:
    void setBusy(bool busy);
    void fail(const QString &message);
    void cleanupTransient();
    void beginExtraction(quint64 generation, const QString &repository,
                         const QString &branch, const QString &archivePath);
    void finishExport(quint64 generation, const QString &repository,
                      const QString &branch, const QString &extractPath);

    QNetworkAccessManager m_network;
    QPointer<QNetworkReply> m_reply;
    QPointer<ProcessRunner> m_runner;
    std::unique_ptr<QTemporaryDir> m_tempDir;
    std::unique_ptr<QSaveFile> m_archiveFile;
    QString m_repository;
    QStringList m_branches;
    bool m_busy = false;
    QString m_statusText;
    QString m_errorText;
    QString m_outputPath;
    QString m_streamError;
    quint64 m_generation = 0;
};
