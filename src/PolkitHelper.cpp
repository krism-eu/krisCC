#include "PolkitHelper.h"

#include "OperationLog.h"

#include <QFileInfo>
#include <QPointer>
#include <QRegularExpression>
#include <QTimer>
#include <QUrl>

#include <signal.h>
#include <unistd.h>

namespace {
constexpr qsizetype kMaxOutput = 256 * 1024;
constexpr qsizetype kMaxLineBuffer = 64 * 1024;
constexpr int kShortTimeoutMs = 2 * 60 * 1000;
constexpr int kRepositoryTimeoutMs = 5 * 60 * 1000;
constexpr int kLongTimeoutMs = 30 * 60 * 1000;
}

PolkitHelper::PolkitHelper(QObject *parent)
    : QObject(parent)
{
}

void PolkitHelper::execute(const QString &program, const QStringList &args)
{
    if (m_running) {
        emit finished(false, tr("Un'altra operazione privilegiata è già in corso."));
        return;
    }

    if (!isPrivilegedInvocationAllowed(program, args)) {
        emit finished(false, tr("Operazione privilegiata non consentita."));
        return;
    }

    m_running = true;
    m_timedOut = false;
    m_allOutput.clear();
    m_lineBuffer.clear();
    m_program = program;
    m_args = args;
    emit runningChanged();

    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::MergedChannels);
    m_process->setChildProcessModifier([] {
        (void)::setsid();
    });
    connect(m_process, &QProcess::readyReadStandardOutput, this, &PolkitHelper::onReadyRead);
    connect(m_process, &QProcess::errorOccurred, this, &PolkitHelper::onProcessError);
    connect(m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, &PolkitHelper::onProcessFinished);

    QStringList fullArgs;
    fullArgs << program << args;
    m_process->start(QStringLiteral("/usr/bin/pkexec"), fullArgs);

    const QPointer<QProcess> guarded = m_process;
    QTimer::singleShot(timeoutFor(program, args), m_process, [this, guarded] {
        if (!guarded || guarded != m_process || guarded->state() == QProcess::NotRunning)
            return;
        m_timedOut = true;
        terminateProcessGroup(false);
        QTimer::singleShot(3000, guarded, [this, guarded] {
            if (guarded && guarded == m_process && guarded->state() != QProcess::NotRunning)
                terminateProcessGroup(true);
        });
    });
}

void PolkitHelper::onReadyRead()
{
    if (m_process)
        consumeOutput(m_process->readAllStandardOutput());
}

void PolkitHelper::onProcessFinished(int exitCode, QProcess::ExitStatus status)
{
    if (!m_process)
        return;

    consumeOutput(m_process->readAllStandardOutput(), true);
    const bool success = !m_timedOut && status == QProcess::NormalExit && exitCode == 0;
    QString output = m_allOutput.trimmed();

    if (m_timedOut) {
        output = tr("Tempo massimo superato: l'operazione privilegiata è stata interrotta.");
    } else if (!success) {
        if (status == QProcess::NormalExit && exitCode == 126)
            output = tr("Autenticazione annullata dall'utente.");
        else if (status == QProcess::NormalExit && exitCode == 127)
            output = tr("Autenticazione amministrativa non disponibile o non autorizzata.");
        else if (output.isEmpty())
            output = tr("Operazione terminata con codice %1.").arg(exitCode);
    }

    OperationLog::append(QStringLiteral("Amministrazione"), operationLabel(),
                         success ? QStringLiteral("success")
                                 : (m_timedOut ? QStringLiteral("timeout") : QStringLiteral("error")));

    m_process->deleteLater();
    m_process = nullptr;
    m_running = false;
    m_timedOut = false;
    m_lineBuffer.clear();
    m_allOutput.clear();
    m_program.clear();
    m_args.clear();
    emit runningChanged();
    emit finished(success, output);
}

void PolkitHelper::onProcessError(QProcess::ProcessError error)
{
    if (!m_process || error != QProcess::FailedToStart)
        return;
    finishWithError(tr("Impossibile avviare pkexec: %1").arg(m_process->errorString()));
}

bool PolkitHelper::isValidPackageName(const QString &package) const
{
    static const QRegularExpression pattern(
        QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._+:-]{0,127}$"));
    return pattern.match(package).hasMatch();
}

bool PolkitHelper::isSafeBootToken(const QString &token) const
{
    static const QRegularExpression pattern(QStringLiteral("^[0-9A-Fa-f]{4}$"));
    return pattern.match(token).hasMatch();
}

bool PolkitHelper::isSafeGrubEntry(const QString &entry) const
{
    if (entry.isEmpty() || entry.size() > 256 || entry.startsWith(QLatin1Char('-')))
        return false;
    for (const QChar ch : entry) {
        if (ch.isNull() || ch.unicode() < 0x20 || ch.unicode() == 0x7f)
            return false;
    }
    return true;
}

bool PolkitHelper::isSafeRepositoryId(const QString &repoId) const
{
    static const QRegularExpression pattern(
        QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._:-]{0,127}$"));
    return pattern.match(repoId).hasMatch();
}

bool PolkitHelper::isSafeRepositoryUrl(const QString &value) const
{
    if (value.isEmpty() || value.size() > 2048
        || value.contains(QRegularExpression(QStringLiteral("[\\s\\x00-\\x1f]"))))
        return false;
    const QUrl url(value);
    return url.isValid()
        && url.scheme() == QStringLiteral("https")
        && !url.host().isEmpty()
        && url.userInfo().isEmpty();
}

bool PolkitHelper::isPrivilegedInvocationAllowed(const QString &program,
                                                  const QStringList &args) const
{
    if (program == QStringLiteral("/usr/bin/rk")) {
        if (args == QStringList{QStringLiteral("sync")})
            return true;
        return args.size() == 2
            && (args.at(0) == QStringLiteral("add")
                || args.at(0) == QStringLiteral("rm")
                || args.at(0) == QStringLiteral("forget"))
            && isValidPackageName(args.at(1));
    }

    if (program == QStringLiteral("/usr/libexec/kriscc/admin")) {
        if (args.isEmpty())
            return false;

        const QString &operation = args.at(0);
        if (args.size() == 1) {
            return operation == QStringLiteral("bootc-check")
                || operation == QStringLiteral("bootc-download")
                || operation == QStringLiteral("bootc-prepare")
                || operation == QStringLiteral("bootc-apply-downloaded");
        }

        if (args.size() != 2)
            return false;

        if ((operation == QStringLiteral("repo-enable")
             || operation == QStringLiteral("repo-disable"))
            && isSafeRepositoryId(args.at(1)))
            return true;
        if (operation == QStringLiteral("repo-add") && isSafeRepositoryUrl(args.at(1)))
            return true;
        if (operation == QStringLiteral("boot-next-uefi") && isSafeBootToken(args.at(1)))
            return true;
        if (operation == QStringLiteral("boot-next-grub") && isSafeGrubEntry(args.at(1)))
            return true;
    }

    return false;
}

int PolkitHelper::timeoutFor(const QString &program, const QStringList &args) const
{
    if (program == QStringLiteral("/usr/bin/rk"))
        return kLongTimeoutMs;
    if (args.isEmpty())
        return kShortTimeoutMs;
    if (args.at(0).startsWith(QStringLiteral("bootc-")))
        return kLongTimeoutMs;
    if (args.at(0).startsWith(QStringLiteral("repo-")))
        return kRepositoryTimeoutMs;
    return kShortTimeoutMs;
}

QString PolkitHelper::operationLabel() const
{
    if (m_program == QStringLiteral("/usr/bin/rk"))
        return m_args.isEmpty() ? QStringLiteral("rk") : QStringLiteral("rk ") + m_args.at(0);
    if (m_program == QStringLiteral("/usr/libexec/kriscc/admin"))
        return m_args.isEmpty() ? QStringLiteral("admin") : m_args.at(0);
    return QFileInfo(m_program).fileName();
}

void PolkitHelper::consumeOutput(const QByteArray &data, bool flushPartial)
{
    if (!data.isEmpty()) {
        m_allOutput += QString::fromUtf8(data);
        if (m_allOutput.size() > kMaxOutput)
            m_allOutput = tr("[output precedente omesso]\n") + m_allOutput.right(kMaxOutput);

        m_lineBuffer += data;
        if (m_lineBuffer.size() > kMaxLineBuffer)
            m_lineBuffer = QByteArray("[riga troppo lunga: inizio omesso]\n")
                         + m_lineBuffer.right(kMaxLineBuffer);
    }

    qsizetype newline = -1;
    while ((newline = m_lineBuffer.indexOf('\n')) >= 0) {
        QByteArray lineData = m_lineBuffer.left(newline);
        m_lineBuffer.remove(0, newline + 1);
        if (!lineData.isEmpty() && lineData.endsWith('\r'))
            lineData.chop(1);
        if (!lineData.isEmpty())
            emit line(QString::fromUtf8(lineData));
    }

    if (flushPartial && !m_lineBuffer.isEmpty()) {
        emit line(QString::fromUtf8(m_lineBuffer));
        m_lineBuffer.clear();
    }
}

void PolkitHelper::terminateProcessGroup(bool force)
{
    if (!m_process || m_process->state() == QProcess::NotRunning)
        return;

    const qint64 pid = m_process->processId();
    if (pid > 0) {
        if (::kill(-pid, force ? SIGKILL : SIGTERM) == 0)
            return;
    }
    force ? m_process->kill() : m_process->terminate();
}

void PolkitHelper::finishWithError(const QString &message)
{
    if (!operationLabel().trimmed().isEmpty())
        OperationLog::append(QStringLiteral("Amministrazione"), operationLabel(),
                             QStringLiteral("error"));

    if (m_process) {
        m_process->deleteLater();
        m_process = nullptr;
    }
    m_running = false;
    m_timedOut = false;
    m_lineBuffer.clear();
    m_allOutput.clear();
    m_program.clear();
    m_args.clear();
    emit runningChanged();
    emit finished(false, message);
}
