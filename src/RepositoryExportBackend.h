#pragma once
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QString>
#include <memory>

class ProcessRunner;
class QNetworkReply;
class QSaveFile;
class QTemporaryDir;

class RepositoryExportBackend final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY stateChanged)
    Q_PROPERTY(QString errorText READ errorText NOTIFY stateChanged)
    Q_PROPERTY(QString outputPath READ outputPath NOTIFY stateChanged)
public:
    explicit RepositoryExportBackend(QObject *parent = nullptr);
    ~RepositoryExportBackend() override;
    bool busy() const { return m_busy; }
    const QString &statusText() const { return m_statusText; }
    const QString &errorText() const { return m_errorText; }
    const QString &outputPath() const { return m_outputPath; }
    Q_INVOKABLE bool exportLatestGreen(const QString &repository);
    Q_INVOKABLE void cancel();
signals:
    void stateChanged();
private:
    void setBusy(bool busy);
    void fail(const QString &message);
    void cleanupTransient();
    void downloadZip(quint64 generation, const QString &repository,
                     const QString &branch, const QString &sha);
    void beginExtraction(quint64 generation, const QString &repository,
                         const QString &branch, const QString &sha,
                         const QString &archivePath);
    void finishExport(quint64 generation, const QString &repository,
                      const QString &branch, const QString &sha,
                      const QString &extractPath);

    QNetworkAccessManager m_network;
    QPointer<QNetworkReply> m_reply;
    QPointer<ProcessRunner> m_runner;
    std::unique_ptr<QTemporaryDir> m_tempDir;
    std::unique_ptr<QSaveFile> m_archiveFile;
    bool m_busy = false;
    QString m_statusText;
    QString m_errorText;
    QString m_outputPath;
    QString m_streamError;
    quint64 m_generation = 0;
};
