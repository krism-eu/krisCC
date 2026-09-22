#include "RepositoryExportBackend.h"
#include "OperationLog.h"
#include "ProcessRunner.h"
#include "RepositoryExportCore.h"
#include <QDir>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUrl>

RepositoryExportBackend::RepositoryExportBackend(QObject *parent) : QObject(parent) {}
RepositoryExportBackend::~RepositoryExportBackend()
{
    if (m_reply) m_reply->abort();
    if (m_runner) m_runner->cancel();
    cleanupTransient();
}
void RepositoryExportBackend::setBusy(bool busy)
{
    if (m_busy == busy) return;
    m_busy = busy;
    emit stateChanged();
}
void RepositoryExportBackend::cleanupTransient()
{
    if (m_reply) { m_reply->deleteLater(); m_reply = nullptr; }
    if (m_runner) { m_runner->deleteLater(); m_runner = nullptr; }
    if (m_archiveFile) { m_archiveFile->cancelWriting(); m_archiveFile.reset(); }
    m_tempDir.reset();
    m_streamError.clear();
}
void RepositoryExportBackend::fail(const QString &message)
{
    cleanupTransient();
    m_errorText = message;
    m_statusText.clear();
    setBusy(false);
    emit stateChanged();
}

bool RepositoryExportBackend::exportLatestGreen(const QString &repository)
{
    if (m_busy) return false;
    const QString slug = RepositoryExportCore::repositorySlug(repository);
    if (slug.isEmpty()) {
        m_errorText = tr("Repository non consentito.");
        emit stateChanged();
        return false;
    }

    ++m_generation;
    const quint64 generation = m_generation;
    cleanupTransient();
    m_errorText.clear();
    m_outputPath.clear();
    m_statusText = tr("Ricerca dell'ultima build verde di %1…").arg(repository);
    setBusy(true);
    emit stateChanged();

    QNetworkRequest request(QUrl(QStringLiteral(
        "https://api.github.com/repos/%1/actions/runs?status=success&per_page=30").arg(slug)));
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("krisCC/%1").arg(KRISCC_VERSION));

    QNetworkReply *reply = m_network.get(request);
    m_reply = reply;
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, generation, repository] {
        if (generation != m_generation || reply != m_reply) return;
        const QByteArray payload = reply->readAll();
        const auto networkError = reply->error();
        const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        m_reply = nullptr;
        reply->deleteLater();
        if (networkError != QNetworkReply::NoError || httpStatus < 200 || httpStatus >= 300) {
            fail(tr("Impossibile leggere le build verdi da GitHub (HTTP %1).").arg(httpStatus));
            return;
        }
        QString parseError;
        const auto run = RepositoryExportCore::parseLatestGreenRun(payload, repository, &parseError);
        if (!run.valid()) {
            fail(parseError.isEmpty() ? tr("Nessuna build verde trovata.") : parseError);
            return;
        }
        downloadZip(generation, repository, run.branch, run.sha);
    });
    return true;
}

void RepositoryExportBackend::downloadZip(quint64 generation, const QString &repository,
                                          const QString &branch, const QString &sha)
{
    if (generation != m_generation) return;
    const QString slug = RepositoryExportCore::repositorySlug(repository);
    m_tempDir = std::make_unique<QTemporaryDir>(
        QDir(QDir::tempPath()).filePath(QStringLiteral("kriscc-repository-export-XXXXXX")));
    if (slug.isEmpty() || !m_tempDir->isValid()) {
        fail(tr("Impossibile preparare l'esportazione."));
        return;
    }

    const QString archivePath = QDir(m_tempDir->path()).filePath(QStringLiteral("repository.zip"));
    m_archiveFile = std::make_unique<QSaveFile>(archivePath);
    if (!m_archiveFile->open(QIODevice::WriteOnly)) {
        fail(tr("Impossibile creare lo ZIP temporaneo."));
        return;
    }

    m_statusText = tr("Download ZIP %1 · %2 · %3…").arg(repository, branch, sha.left(12));
    emit stateChanged();

    QNetworkRequest request(QUrl(QStringLiteral("https://github.com/%1/archive/%2.zip").arg(slug, sha)));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("krisCC/%1").arg(KRISCC_VERSION));
    QNetworkReply *reply = m_network.get(request);
    m_reply = reply;

    connect(reply, &QIODevice::readyRead, this, [this, reply, generation] {
        if (generation != m_generation || reply != m_reply || !m_archiveFile) return;
        const QByteArray data = reply->readAll();
        if (!data.isEmpty() && m_archiveFile->write(data) != data.size()) {
            m_streamError = tr("Errore scrivendo lo ZIP temporaneo.");
            reply->abort();
        }
    });
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, generation, repository, branch, sha, archivePath] {
        if (generation != m_generation || reply != m_reply) return;
        if (m_archiveFile) {
            const QByteArray remaining = reply->readAll();
            if (!remaining.isEmpty() && m_archiveFile->write(remaining) != remaining.size())
                m_streamError = tr("Errore scrivendo lo ZIP temporaneo.");
        }
        const auto networkError = reply->error();
        const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        m_reply = nullptr;
        reply->deleteLater();
        if (!m_streamError.isEmpty()) { fail(m_streamError); return; }
        if (networkError != QNetworkReply::NoError || httpStatus < 200 || httpStatus >= 300) {
            fail(tr("Download ZIP non riuscito (HTTP %1).").arg(httpStatus));
            return;
        }
        if (!m_archiveFile || !m_archiveFile->commit()) {
            fail(tr("Impossibile finalizzare lo ZIP temporaneo."));
            return;
        }
        m_archiveFile.reset();
        beginExtraction(generation, repository, branch, sha, archivePath);
    });
}

void RepositoryExportBackend::beginExtraction(quint64 generation, const QString &repository,
                                              const QString &branch, const QString &sha,
                                              const QString &archivePath)
{
    if (generation != m_generation || !m_tempDir) return;
    const QString python = QStandardPaths::findExecutable(QStringLiteral("python3"));
    if (python.isEmpty()) {
        fail(tr("python3 non disponibile: impossibile estrarre lo ZIP."));
        return;
    }
    const QString extractPath = QDir(m_tempDir->path()).filePath(QStringLiteral("extract"));
    if (!QDir().mkpath(extractPath)) {
        fail(tr("Impossibile preparare la directory di estrazione."));
        return;
    }

    m_statusText = tr("Estrazione dello ZIP…");
    emit stateChanged();
    auto *runner = new ProcessRunner(this);
    m_runner = runner;
    connect(runner, &ProcessRunner::finished, this,
            [this, runner, generation, repository, branch, sha, extractPath]
            (ProcessRunner::Outcome outcome, int, const QByteArray &,
             const QByteArray &, const QString &error) {
        if (generation != m_generation || runner != m_runner) return;
        m_runner = nullptr;
        runner->deleteLater();
        if (outcome != ProcessRunner::Success) {
            fail(error.isEmpty() ? tr("Impossibile estrarre lo ZIP.") : error);
            return;
        }
        finishExport(generation, repository, branch, sha, extractPath);
    });

    ProcessRunner::Options options;
    options.program = python;
    options.arguments = {QStringLiteral("-m"), QStringLiteral("zipfile"), QStringLiteral("-e"),
                         archivePath, extractPath};
    options.timeoutMs = 2 * 60 * 1000;
    options.maxOutputBytes = 64 * 1024;
    options.mergedChannels = true;
    options.processGroup = true;
    if (!runner->start(options))
        fail(tr("Impossibile avviare l'estrazione ZIP."));
}

void RepositoryExportBackend::finishExport(quint64 generation, const QString &repository,
                                           const QString &branch, const QString &sha,
                                           const QString &extractPath)
{
    if (generation != m_generation || !m_tempDir) return;
    QDir extractDir(extractPath);
    const QStringList roots = extractDir.entryList(
        QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot, QDir::Name);
    const QString repositoryRoot = roots.size() == 1
        ? extractDir.filePath(roots.constFirst()) : extractPath;

    const QString destination = QDir(QDir::homePath()).filePath(
        QStringLiteral("%1-last-green-%2-%3.txt")
            .arg(repository, RepositoryExportCore::safeFileComponent(branch), sha.left(12)));

    int textFiles = 0, binaryFiles = 0;
    QString error;
    if (!RepositoryExportCore::writeCombinedRepository(
            repositoryRoot, repository, branch, sha, destination,
            &textFiles, &binaryFiles, &error)) {
        fail(error);
        return;
    }

    cleanupTransient();
    m_outputPath = destination;
    m_errorText.clear();
    m_statusText = tr("Esportazione completata da ultima build verde: %1 · %2 · %3. %4 file testuali, %5 binari/symlink annotati.")
        .arg(repository, branch, sha.left(12)).arg(textFiles).arg(binaryFiles);
    setBusy(false);
    OperationLog::append(QStringLiteral("Repository"), QStringLiteral("export-last-green"),
                         QStringLiteral("success"),
                         repository + QLatin1Char('@') + branch + QLatin1Char('#') + sha.left(12));
    emit stateChanged();
}

void RepositoryExportBackend::cancel()
{
    if (!m_busy && !m_reply && !m_runner) return;
    ++m_generation;
    if (m_reply) m_reply->abort();
    if (m_runner) m_runner->cancel();
    cleanupTransient();
    m_errorText.clear();
    m_statusText = tr("Operazione annullata.");
    setBusy(false);
    emit stateChanged();
}
