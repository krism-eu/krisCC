#include "CronBackend.h"

#include "CronParser.h"
#include "ProcessRunner.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QVariantMap>

namespace {
constexpr int kCronTimeoutMs = 3000;
constexpr qint64 kMaxCronFileBytes = 256 * 1024;
constexpr qsizetype kMaxCronOutputBytes = 256 * 1024;

bool readableCronFile(const QFileInfo &info)
{
    return info.exists() && info.isFile() && info.isReadable() && !info.isSymLink()
        && info.size() >= 0 && info.size() <= kMaxCronFileBytes;
}
}

CronBackend::CronBackend(QObject *parent)
    : QObject(parent)
{
    reload();
}

CronBackend::~CronBackend() = default;

QVariantList CronBackend::jobsFromText(const QString &text, bool systemFormat,
                                       const QString &scope, const QString &source) const
{
    QVariantList result;
    const QList<CronParser::Entry> entries = CronParser::parse(text, systemFormat);
    result.reserve(entries.size());

    for (const CronParser::Entry &entry : entries) {
        QVariantMap job;
        job.insert(QStringLiteral("scope"), scope);
        job.insert(QStringLiteral("source"), source);
        job.insert(QStringLiteral("schedule"), entry.schedule);
        job.insert(QStringLiteral("summary"), CronParser::describe(entry.schedule));
        job.insert(QStringLiteral("command"), entry.command);
        job.insert(QStringLiteral("user"), entry.user);
        result.append(job);
    }

    return result;
}

void CronBackend::loadSystemJobs()
{
    const auto appendFile = [this](const QString &path) {
        const QFileInfo info(path);
        if (!readableCronFile(info))
            return;

        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return;

        const QString text = QString::fromUtf8(file.readAll());
        m_jobs += jobsFromText(text, true, QStringLiteral("system"), path);
    };

    appendFile(QStringLiteral("/etc/crontab"));

    QDir cronDirectory(QStringLiteral("/etc/cron.d"));
    const QFileInfoList files = cronDirectory.entryInfoList(
        QDir::Files | QDir::Readable | QDir::NoDotAndDotDot, QDir::Name);
    static const QRegularExpression cronFileName(
        QStringLiteral("^[A-Za-z0-9_-]+$"));
    for (const QFileInfo &info : files) {
        if (!cronFileName.match(info.fileName()).hasMatch())
            continue;
        if (readableCronFile(info))
            appendFile(info.absoluteFilePath());
    }
}

void CronBackend::reload()
{
    if (m_busy)
        return;

    m_jobs.clear();
    m_errorText.clear();
    m_userCronStatus.clear();
    loadSystemJobs();

    const QString crontabProgram = QStandardPaths::findExecutable(QStringLiteral("crontab"));
    m_userCronAvailable = !crontabProgram.isEmpty();

    if (!m_userCronAvailable) {
        m_userCronStatus = tr("Comando crontab non disponibile: vengono mostrate solo le fonti di sistema leggibili.");
        emit stateChanged();
        return;
    }

    auto *runner = new ProcessRunner(this);
    m_runner = runner;
    m_busy = true;
    m_userCronStatus = tr("Lettura del crontab utente…");
    emit stateChanged();

    connect(runner, &ProcessRunner::finished, this,
            [this, runner](ProcessRunner::Outcome outcome, int exitCode,
                           const QByteArray &standardOutput,
                           const QByteArray &standardError,
                           const QString &) {
        if (m_runner != runner)
            return;
        m_runner = nullptr;
        m_busy = false;
        finishUserCron(standardOutput, standardError, exitCode, int(outcome));
        runner->deleteLater();
        emit stateChanged();
    });

    ProcessRunner::Options options;
    options.program = crontabProgram;
    options.arguments = {QStringLiteral("-l")};
    options.timeoutMs = kCronTimeoutMs;
    options.maxOutputBytes = kMaxCronOutputBytes;
    options.mergedChannels = false;

    if (!runner->start(options)) {
        m_runner = nullptr;
        m_busy = false;
        m_userCronStatus = tr("Impossibile avviare la lettura del crontab utente.");
        m_errorText = m_userCronStatus;
        runner->deleteLater();
        emit stateChanged();
    }
}

void CronBackend::finishUserCron(const QByteArray &standardOutput,
                                 const QByteArray &standardError,
                                 int exitCode, int outcome)
{
    const auto result = static_cast<ProcessRunner::Outcome>(outcome);
    const QString stderrText = QString::fromUtf8(standardError).trimmed();

    if (result == ProcessRunner::Success) {
        QVariantList userJobs = jobsFromText(QString::fromUtf8(standardOutput), false,
                                             QStringLiteral("user"),
                                             tr("Crontab utente"));
        const bool hasUserJobs = !userJobs.isEmpty();
        userJobs += m_jobs;
        m_jobs = userJobs;
        m_userCronStatus = hasUserJobs
            ? tr("Crontab utente letto.")
            : tr("Nessun job cron utente.");
        return;
    }

    const bool noUserCrontab = result == ProcessRunner::ExitError
        && exitCode == 1 && standardOutput.trimmed().isEmpty();
    if (noUserCrontab) {
        m_userCronStatus = tr("Nessun crontab utente disponibile.");
        return;
    }

    if (result == ProcessRunner::TimedOut)
        m_errorText = tr("La lettura del crontab utente ha superato il tempo massimo.");
    else if (result == ProcessRunner::Cancelled)
        m_errorText = tr("Lettura del crontab utente annullata.");
    else if (!stderrText.isEmpty())
        m_errorText = tr("Impossibile leggere il crontab utente: %1").arg(stderrText.left(300));
    else
        m_errorText = tr("Impossibile leggere il crontab utente.");

    m_userCronStatus = m_errorText;
}

bool CronBackend::cancel()
{
    return m_runner && m_runner->cancel();
}
