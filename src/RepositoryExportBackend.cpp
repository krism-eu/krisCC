#include "RepositoryExportBackend.h"

#include "OperationLog.h"
#include "ProcessRunner.h"
#include "RepositoryExportCore.h"

#include <QDateTime>
#include <QDir>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QUrl>

RepositoryExportBackend::RepositoryExportBackend(QObject *parent)
    : QObject(parent)
{
}

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
    if (m_reply) {
        m_reply->deleteLater();
        m_reply = nullptr;
    }
    if (m_runner) {
        m_runner->deleteLater();
        m_runner = nullptr;
    }
    if (m_archiveFile) {
        m_archiveFile->cancelWriting();
        m_archiveFile.reset();
    }
    m_tempDir.reset();
    m_streamError.clear();
}

void RepositoryExportBackend::fail(const QString &message)
{
    cleanupTransient();
    m_errorText = message;
    m_statusText.clear();
    m_commitSha.clear();
    setBusy(false);
    emit stateChanged();
}

bool RepositoryExportBackend::refreshBranches(const QString &repository)
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
    m_repository = repository;
    m_branches.clear();
    m_errorText.clear();
    m_outputPath.clear();
    m_commitSha.clear();
    m_statusText = tr("Caricamento branch…");
    emit branchesChanged();
    setBusy(true);
    emit stateChanged();

    QNetworkRequest request(QUrl(QStringLiteral("https://api.github.com/repos/%1/branches?per_page=100").arg(slug)));
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("krisCC/%1").arg(KRISCC_VERSION));

    QNetworkReply *reply = m_network.get(request);
    m_reply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation] {
        if (generation != m_generation || reply != m_reply) return;
        const QByteArray payload = reply->readAll();
        const auto networkError = reply->error();
        const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        m_reply = nullptr;
        reply->deleteLater();

        if (networkError != QNetworkReply::NoError || httpStatus < 200 || httpStatus >= 300) {
            fail(tr("Impossibile leggere i branch da GitHub (HTTP %1).").arg(httpStatus));
            return;
        }

        QString parseError;
        const QStringList parsed = RepositoryExportCore::parseBranchList(payload, &parseError);
        if (!parseError.isEmpty()) {
            fail(parseError);
            return;
        }

        m_branches = parsed;
        m_errorText.clear();
        m_statusText = parsed.isEmpty()
            ? tr("Nessun branch disponibile.")
            : tr("%1 branch disponibili.").arg(parsed.size());
        setBusy(false);
        emit branchesChanged();
        emit stateChanged();
    });
    return true;
}

bool RepositoryExportBackend::exportBranch(const QString &repository, const QString &branch)
{
    if (m_busy) return false;
    const QString slug = RepositoryExportCore::repositorySlug(repository);
    const QString selectedBranch = branch.trimmed();
    if (slug.isEmpty() || repository != m_repository
        || selectedBranch.isEmpty() || !m_branches.contains(selectedBranch)) {
        m_errorText = tr("Seleziona un repository e un branch caricati da GitHub.");
        emit stateChanged();
        return false;
    }

    ++m_generation;
    const quint64 generation = m_generation;
    cleanupTransient();
    m_errorText.clear();
    m_outputPath.clear();
    m_commitSha.clear();
    m_statusText = tr("Risoluzione del commit esatto di %1 · %2…").arg(repository, selectedBranch);
    setBusy(true);
    emit stateChanged();

    const QByteArray encodedUrl = QByteArray("https://api.github.com/repos/")
        + slug.toUtf8() + QByteArray("/commits/") + QUrl::toPercentEncoding(selectedBranch);
    QNetworkRequest request(QUrl::fromEncoded(encodedUrl));
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("krisCC/%1").arg(KRISCC_VERSION));

    QNetworkReply *reply = m_network.get(request);
    m_reply = reply;
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, generation, repository, selectedBranch, slug] {
        if (generation != m_generation || reply != m_reply) return;
        const QByteArray payload = reply->readAll();
        const auto networkError = reply->error();
        const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        m_reply = nullptr;
        reply->deleteLater();

        if (networkError != QNetworkReply::NoError || httpStatus < 200 || httpStatus >= 300) {
            fail(tr("Impossibile risolvere il commit del branch (HTTP %1).").arg(httpStatus));
            return;
        }

        QString parseError;
        const QString commit = RepositoryExportCore::parseCommitSha(payload, &parseError);
        if (!parseError.isEmpty() || commit.isEmpty()) {
            fail(parseError.isEmpty() ? tr("Commit GitHub non valido.") : parseError);
            return;
        }

        m_commitSha = commit;
        m_statusText = tr("Commit risolto: %1. Download del repository…").arg(commit.left(12));
        emit stateChanged();
        beginArchiveDownload(generation, repository, selectedBranch, commit);
    });
    return true;
}

void RepositoryExportBackend::beginArchiveDownload(quint64 generation,
                                                   const QString &repository,
                                                   const QString &branch,
                                                   const QString &commitSha)
{
    if (generation != m_generation) return;
    const QString slug = RepositoryExportCore::repositorySlug(repository);
    if (slug.isEmpty()) {
        fail(tr("Repository non consentito."));
        return;
    }

    m_tempDir = std::make_unique<QTemporaryDir>(
        QDir(QDir::tempPath()).filePath(QStringLiteral("kriscc-repository-export-XXXXXX")));
    if (!m_tempDir->isValid()) {
        fail(tr("Impossibile creare la directory temporanea."));
        return;
    }

    const QString archivePath = QDir(m_tempDir->path()).filePath(QStringLiteral("repository.tar.gz"));
    m_archiveFile = std::make_unique<QSaveFile>(archivePath);
    if (!m_archiveFile->open(QIODevice::WriteOnly)) {
        fail(tr("Impossibile creare l'archivio temporaneo."));
        return;
    }

    const QByteArray encodedUrl = QByteArray("https://api.github.com/repos/")
        + slug.toUtf8() + QByteArray("/tarball/") + commitSha.toUtf8();
    QNetworkRequest request(QUrl::fromEncoded(encodedUrl));
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
            m_streamError = tr("Errore scrivendo l'archivio temporaneo.");
            reply->abort();
        }
    });
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, generation, repository, branch, commitSha, archivePath] {
        if (generation != m_generation || reply != m_reply) return;
        if (m_archiveFile) {
            const QByteArray remaining = reply->readAll();
            if (!remaining.isEmpty() && m_archiveFile->write(remaining) != remaining.size())
                m_streamError = tr("Errore scrivendo l'archivio temporaneo.");
        }

        const auto networkError = reply->error();
        const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        m_reply = nullptr;
        reply->deleteLater();

        if (!m_streamError.isEmpty()) {
            fail(m_streamError);
            return;
        }
        if (networkError != QNetworkReply::NoError || httpStatus < 200 || httpStatus >= 300) {
            fail(tr("Download repository non riuscito (HTTP %1).").arg(httpStatus));
            return;
        }
        if (!m_archiveFile || !m_archiveFile->commit()) {
            fail(tr("Impossibile finalizzare l'archivio temporaneo."));
            return;
        }
        m_archiveFile.reset();
        beginExtraction(generation, repository, branch, commitSha, archivePath);
    });
}

void RepositoryExportBackend::beginExtraction(quint64 generation,
                                              const QString &repository,
                                              const QString &branch,
                                              const QString &commitSha,
                                              const QString &archivePath)
{
    if (generation != m_generation || !m_tempDir) return;
    const QString extractPath = QDir(m_tempDir->path()).filePath(QStringLiteral("extract"));
    if (!QDir().mkpath(extractPath)) {
        fail(tr("Impossibile preparare la directory di estrazione."));
        return;
    }

    m_statusText = tr("Estrazione del commit %1…").arg(commitSha.left(12));
    emit stateChanged();

    auto *runner = new ProcessRunner(this);
    m_runner = runner;
    connect(runner, &ProcessRunner::finished, this,
            [this, runner, generation, repository, branch, commitSha, extractPath]
            (ProcessRunner::Outcome outcome, int,
             const QByteArray &, const QByteArray &, const QString &error) {
        if (generation != m_generation || runner != m_runner) return;
        m_runner = nullptr;
        runner->deleteLater();
        if (outcome != ProcessRunner::Success) {
            fail(error.isEmpty() ? tr("Impossibile estrarre il repository.") : error);
            return;
        }
        finishExport(generation, repository, branch, commitSha, extractPath);
    });

    ProcessRunner::Options options;
    options.program = QStringLiteral("/usr/bin/tar");
    options.arguments = {
        QStringLiteral("--extract"), QStringLiteral("--gzip"),
        QStringLiteral("--file"), archivePath,
        QStringLiteral("--directory"), extractPath,
        QStringLiteral("--no-same-owner"), QStringLiteral("--no-same-permissions")
    };
    options.timeoutMs = 2 * 60 * 1000;
    options.maxOutputBytes = 64 * 1024;
    options.mergedChannels = true;
    options.processGroup = true;
    if (!runner->start(options))
        fail(tr("Impossibile avviare tar."));
}

void RepositoryExportBackend::finishExport(quint64 generation,
                                           const QString &repository,
                                           const QString &branch,
                                           const QString &commitSha,
                                           const QString &extractPath)
{
    if (generation != m_generation || !m_tempDir) return;
    QDir extractDir(extractPath);
    const QStringList roots = extractDir.entryList(
        QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot, QDir::Name);
    const QString repositoryRoot = roots.size() == 1
        ? extractDir.filePath(roots.constFirst()) : extractPath;

    const QString filename = QStringLiteral("%1-%2-%3.txt")
        .arg(repository,
             RepositoryExportCore::safeFileComponent(branch),
             commitSha.left(12));
    const QString destination = QDir(QDir::homePath()).filePath(filename);

    int textFiles = 0;
    int binaryFiles = 0;
    QString error;
    if (!RepositoryExportCore::writeCombinedRepository(
            repositoryRoot, repository, branch, commitSha, destination,
            &textFiles, &binaryFiles, &error)) {
        fail(error);
        return;
    }

    cleanupTransient();
    m_commitSha = commitSha;
    m_outputPath = destination;
    m_errorText.clear();
    m_statusText = tr("Esportazione %1 completata: %2 file testuali, %3 file binari/symlink annotati.")
        .arg(commitSha.left(12)).arg(textFiles).arg(binaryFiles);
    setBusy(false);
    OperationLog::append(QStringLiteral("Repository"), QStringLiteral("export"),
                         QStringLiteral("success"),
                         repository + QLatin1Char('@') + branch + QLatin1Char('#') + commitSha.left(12));
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
    m_commitSha.clear();
    m_statusText = tr("Operazione annullata.");
    setBusy(false);
    emit stateChanged();
}
