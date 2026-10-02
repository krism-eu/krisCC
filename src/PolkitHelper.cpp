#include "PolkitHelper.h"
#include "OperationLog.h"
#include "AdminPolicy.h"

#include <QFileInfo>
#include <QRegularExpression>

namespace {
constexpr qsizetype kMaxOutput = 256 * 1024;
constexpr qsizetype kMaxLineBuffer = 64 * 1024;
struct AdminProtocolOutput {
    QString output;
    QString status;
};

bool adminStatusLine(const QString &line, QString *status = nullptr)
{
    static const QString prefix = QStringLiteral("KRISCC_ADMIN_STATUS ");
    if (!line.startsWith(prefix))
        return false;

    const QString value = line.mid(prefix.size());
    static const QRegularExpression pattern(
        QStringLiteral("^(?:timeout|failed-to-start|descendants-alive|crashed|child [0-9]{1,3})$"));
    if (!pattern.match(value).hasMatch())
        return false;

    if (status)
        *status = value;
    return true;
}

AdminProtocolOutput parseAdminOutput(const QString &raw)
{
    AdminProtocolOutput result;
    QStringList visibleLines;
    const QStringList lines = raw.split(QLatin1Char('\n'), Qt::KeepEmptyParts);
    visibleLines.reserve(lines.size());

    for (QString line : lines) {
        if (line.endsWith(QLatin1Char('\r')))
            line.chop(1);

        QString status;
        if (adminStatusLine(line, &status)) {
            // The helper status is emitted only after the supervised command/group
            // has stopped. The last valid marker therefore identifies the source
            // of the exit code without conflating it with pkexec or child codes.
            result.status = status;
            continue;
        }
        visibleLines.append(line);
    }

    result.output = visibleLines.join(QLatin1Char('\n')).trimmed();
    return result;
}
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
    m_allOutput.clear();
    m_lineBuffer.clear();
    m_program = program;
    m_args = args;
    emit runningChanged();

    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::MergedChannels);
    m_process->setStandardInputFile(QProcess::nullDevice());
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
    const AdminProtocolOutput protocol = parseAdminOutput(m_allOutput);

    const QString childPrefix = QStringLiteral("child ");
    bool childCodeOk = false;
    const int protocolChildCode = protocol.status.startsWith(childPrefix)
        ? protocol.status.mid(childPrefix.size()).toInt(&childCodeOk)
        : -1;
    const bool protocolMatchesExit =
        protocol.status.isEmpty()
        || (protocol.status == QStringLiteral("timeout") && exitCode == 124)
        || ((protocol.status == QStringLiteral("failed-to-start")
             || protocol.status == QStringLiteral("descendants-alive")
             || protocol.status == QStringLiteral("crashed"))
            && exitCode == 125)
        || (childCodeOk && protocolChildCode == exitCode);
    const bool success = status == QProcess::NormalExit && exitCode == 0
        && protocolMatchesExit;
    QString output = protocol.output;

    if (!success) {
        if (status == QProcess::NormalExit && !protocolMatchesExit) {
            output = tr("Risultato incoerente dall'helper amministrativo (codice %1).").arg(exitCode);
        } else if (status == QProcess::NormalExit && protocol.status == QStringLiteral("timeout")) {
            output = tr("Tempo massimo superato: l'helper amministrativo ha interrotto l'operazione.");
        } else if (status == QProcess::NormalExit
                   && protocol.status == QStringLiteral("failed-to-start")) {
            output = tr("L'helper amministrativo non è riuscito ad avviare il comando.");
        } else if (status == QProcess::NormalExit
                   && protocol.status == QStringLiteral("descendants-alive")) {
            output = tr("Il comando amministrativo ha lasciato processi discendenti attivi ed è stato interrotto.");
        } else if (status == QProcess::NormalExit
                   && protocol.status == QStringLiteral("crashed")) {
            output = tr("Il comando amministrativo è terminato in modo anomalo.");
        } else if (status == QProcess::NormalExit
                   && childCodeOk && protocolChildCode == exitCode) {
            if (output.isEmpty())
                output = tr("Comando amministrativo terminato con codice %1.").arg(exitCode);
        } else if (status == QProcess::NormalExit && protocol.status.isEmpty()
                   && exitCode == 126) {
            output = tr("Autenticazione annullata dall'utente.");
        } else if (status == QProcess::NormalExit && protocol.status.isEmpty()
                   && exitCode == 127) {
            output = tr("Autorizzazione amministrativa non ottenuta oppure errore di pkexec.");
        } else if (output.isEmpty()) {
            output = tr("Operazione terminata con codice %1.").arg(exitCode);
        }

        output = userFacingOutput(output);
    }

    OperationLog::append(QStringLiteral("Amministrazione"), operationLabel(),
                         success ? QStringLiteral("success")
                                 : (protocol.status == QStringLiteral("timeout")
                                        ? QStringLiteral("timeout")
                                        : QStringLiteral("error")));

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

bool PolkitHelper::isPrivilegedInvocationAllowed(const QString &program,
                                                  const QStringList &args) const
{
    return program == QStringLiteral("/usr/libexec/kriscc/admin")
        && AdminPolicy::resolve(args).has_value();
}

QString PolkitHelper::operationLabel() const
{
    if (m_program == QStringLiteral("/usr/libexec/kriscc/admin"))
        return m_args.isEmpty() ? QStringLiteral("admin") : m_args.at(0);
    return QFileInfo(m_program).fileName();
}

QString PolkitHelper::userFacingOutput(const QString &output) const
{
    static const QString dnfLockMarker =
        QStringLiteral("Another package transaction holds the DNF system lock");

    if (!output.contains(dnfLockMarker, Qt::CaseInsensitive))
        return output;

    QStringList lines = output.split(QLatin1Char('\n'));
    const QString friendly =
        tr("Un'altra operazione sui pacchetti è in corso. "
           "Attendi che termini e riprova.");

    for (QString &line : lines) {
        if (line.contains(dnfLockMarker, Qt::CaseInsensitive))
            line = friendly;
    }

    return lines.join(QLatin1Char('\n'));
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
        if (!lineData.isEmpty()) {
            const QString lineText = QString::fromUtf8(lineData);
            if (!adminStatusLine(lineText))
                emit line(userFacingOutput(lineText));
        }
    }

    if (flushPartial && !m_lineBuffer.isEmpty()) {
        const QString lineText = QString::fromUtf8(m_lineBuffer);
        if (!adminStatusLine(lineText))
            emit line(userFacingOutput(lineText));
        m_lineBuffer.clear();
    }
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
    m_lineBuffer.clear();
    m_allOutput.clear();
    m_program.clear();
    m_args.clear();
    emit runningChanged();
    emit finished(false, message);
}
