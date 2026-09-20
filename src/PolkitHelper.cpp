#include "PolkitHelper.h"

#include "OperationLog.h"

#include <QDebug>
#include <QFileInfo>
#include <QRegularExpression>
#include <QUrl>

PolkitHelper::PolkitHelper(QObject *parent)
    : QObject(parent)
{
}

void PolkitHelper::execute(const QString &program, const QStringList &args)
{
    if (m_running) {
        emit finished(false, tr("Un'altra operazione privilegiata e' gia' in corso."));
        return;
    }

    if (!isPrivilegedInvocationAllowed(program, args)) {
        emit finished(false, tr("Operazione privilegiata non consentita."));
        return;
    }

    m_running = true;
    m_allOutput.clear();
    m_lineBuffer.clear();
    m_program = program;
    m_args = args;
    emit runningChanged();

    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_process, &QProcess::readyReadStandardOutput, this, &PolkitHelper::onReadyRead);
    connect(m_process, &QProcess::errorOccurred, this, &PolkitHelper::onProcessError);
    connect(m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, &PolkitHelper::onProcessFinished);

    QStringList fullArgs;
    fullArgs << program << args;
    m_process->start(QStringLiteral("/usr/bin/pkexec"), fullArgs);
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
    const bool success = status == QProcess::NormalExit && exitCode == 0;
    QString output = m_allOutput.trimmed();

    if (!success) {
        if (status == QProcess::NormalExit && exitCode == 126)
            output = tr("Autenticazione annullata dall'utente.");
        else if (status == QProcess::NormalExit && exitCode == 127)
            output = tr("Autenticazione amministrativa non disponibile o non autorizzata.");
        else if (output.isEmpty())
            output = tr("Operazione terminata con codice %1.").arg(exitCode);
    }

    const QString action = QFileInfo(m_program).fileName()
        + (m_args.isEmpty() ? QString() : QStringLiteral(" ") + m_args.join(QLatin1Char(' ')));
    OperationLog::append(QStringLiteral("Amministrazione"), action,
                         success ? QStringLiteral("success") : QStringLiteral("error"));

    m_process->deleteLater();
    m_process = nullptr;
    m_running = false;
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
    static const QRegularExpression pattern(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._+:-]{0,127}$"));
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
    static const QRegularExpression pattern(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._:-]{0,127}$"));
    return pattern.match(repoId).hasMatch();
}

bool PolkitHelper::isSafeRepositoryUrl(const QString &value) const
{
    if (value.isEmpty() || value.size() > 2048 || value.contains(QRegularExpression(QStringLiteral("[\\s\\x00-\\x1f]"))))
        return false;
    const QUrl url(value);
    return url.isValid()
        && url.scheme() == QStringLiteral("https")
        && !url.host().isEmpty()
        && url.userInfo().isEmpty();
}

bool PolkitHelper::isPrivilegedInvocationAllowed(const QString &program, const QStringList &args) const
{
    if (program == QStringLiteral("/usr/bin/rk")) {
        if (args == QStringList{QStringLiteral("sync")})
            return true;
        if (args.size() == 2
            && (args.at(0) == QStringLiteral("add")
                || args.at(0) == QStringLiteral("rm")
                || args.at(0) == QStringLiteral("forget")))
            return isValidPackageName(args.at(1));
        return false;
    }

    if (program == QStringLiteral("/usr/bin/bootc")) {
        static const QList<QStringList> allowed = {
            {QStringLiteral("upgrade")},
            {QStringLiteral("upgrade"), QStringLiteral("--check")},
            {QStringLiteral("upgrade"), QStringLiteral("--download-only")},
            {QStringLiteral("upgrade"), QStringLiteral("--from-downloaded")},
            {QStringLiteral("upgrade"), QStringLiteral("--from-downloaded"), QStringLiteral("--apply")}
        };
        return allowed.contains(args);
    }

    if (program == QStringLiteral("/usr/bin/dnf5")) {
        if (args.size() == 3
            && args.at(0) == QStringLiteral("config-manager")
            && (args.at(1) == QStringLiteral("enable") || args.at(1) == QStringLiteral("disable")))
            return isSafeRepositoryId(args.at(2));

        const QString prefix = QStringLiteral("--from-repofile=");
        if (args.size() == 3
            && args.at(0) == QStringLiteral("config-manager")
            && args.at(1) == QStringLiteral("addrepo")
            && args.at(2).startsWith(prefix))
            return isSafeRepositoryUrl(args.at(2).mid(prefix.size()));
        return false;
    }

    if (program == QStringLiteral("/usr/libexec/kriscc/maintenance")) {
        static const QList<QStringList> allowed = {
            {QStringLiteral("trash-home")},
            {QStringLiteral("trash-system")},
            {QStringLiteral("trash-all")}
        };
        return allowed.contains(args);
    }

    if (program == QStringLiteral("/usr/bin/efibootmgr"))
        return args.size() == 2 && args.at(0) == QStringLiteral("-n")
            && isSafeBootToken(args.at(1));

    if (program == QStringLiteral("/usr/bin/grub2-reboot"))
        return args.size() == 1 && isSafeGrubEntry(args.at(0));

    return false;
}

void PolkitHelper::consumeOutput(const QByteArray &data, bool flushPartial)
{
    if (!data.isEmpty()) {
        m_allOutput += QString::fromUtf8(data);
        m_lineBuffer += data;
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

void PolkitHelper::finishWithError(const QString &message)
{
    const QString action = QFileInfo(m_program).fileName()
        + (m_args.isEmpty() ? QString() : QStringLiteral(" ") + m_args.join(QLatin1Char(' ')));
    if (!action.trimmed().isEmpty())
        OperationLog::append(QStringLiteral("Amministrazione"), action, QStringLiteral("error"));

    if (m_process) {
        m_process->deleteLater();
        m_process = nullptr;
    }
    m_running = false;
    m_lineBuffer.clear();
    m_allOutput.clear();
    m_program.clear();
    m_args.clear();
    emit runningChanged();
    emit finished(false, message);
}
