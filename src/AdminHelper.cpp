#include "AdminPolicy.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QProcess>
#include <QTextStream>

#include <signal.h>
#include <unistd.h>

namespace {
using ProcessMap = QHash<qint64, quint64>;

void reportStatus(QTextStream &err, const QString &status)
{
    err << "KRISCC_ADMIN_STATUS " << status << '\n';
    err.flush();
}

ProcessMap processesInGroup(qint64 pgid)
{
    ProcessMap result;
    if (pgid <= 0)
        return result;

    const QDir proc(QStringLiteral("/proc"));
    const QStringList entries = proc.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &entry : entries) {
        bool pidOk = false;
        const qint64 pid = entry.toLongLong(&pidOk);
        if (!pidOk || pid <= 0)
            continue;

        QFile statFile(proc.filePath(entry + QStringLiteral("/stat")));
        if (!statFile.open(QIODevice::ReadOnly))
            continue;
        const QByteArray line = statFile.readAll().trimmed();
        const qsizetype closeParen = line.lastIndexOf(')');
        if (closeParen < 0 || closeParen + 2 >= line.size())
            continue;
        const QList<QByteArray> fields = line.mid(closeParen + 2).split(' ');
        if (fields.size() <= 19)
            continue;

        const QByteArray &stateField = fields.at(0);
        const char state = stateField.isEmpty() ? '\0' : stateField.at(0);
        if (state == 'Z' || state == 'X' || state == 'x')
            continue;

        bool groupOk = false;
        bool startOk = false;
        const qint64 processGroup = fields.at(2).toLongLong(&groupOk);
        const quint64 startTime = fields.at(19).toULongLong(&startOk);
        if (groupOk && startOk && processGroup == pgid)
            result.insert(pid, startTime);
    }
    return result;
}

bool mapsShareIdentity(const ProcessMap &known, const ProcessMap &current)
{
    for (auto it = current.cbegin(); it != current.cend(); ++it) {
        const auto knownIt = known.constFind(it.key());
        if (knownIt != known.cend() && knownIt.value() == it.value())
            return true;
    }
    return false;
}

void refreshTrackedGroup(qint64 pgid, ProcessMap *tracked, bool allowSeed)
{
    const ProcessMap current = processesInGroup(pgid);
    if (current.isEmpty())
        return;
    if (!mapsShareIdentity(*tracked, current) && !(allowSeed && tracked->isEmpty()))
        return;
    for (auto it = current.cbegin(); it != current.cend(); ++it)
        tracked->insert(it.key(), it.value());
}

bool trackedGroupAlive(qint64 pgid, const ProcessMap &tracked)
{
    if (pgid <= 0 || tracked.isEmpty())
        return false;
    return mapsShareIdentity(tracked, processesInGroup(pgid));
}

bool signalTrackedGroup(qint64 pgid, ProcessMap *tracked, int signalNumber,
                        bool allowSeed = false)
{
    refreshTrackedGroup(pgid, tracked, allowSeed);
    if (!trackedGroupAlive(pgid, *tracked))
        return false;
    return ::kill(-pgid, signalNumber) == 0;
}

int runProgram(const AdminPolicy::Command &command)
{
    QTextStream err(stderr);
    QProcess process;
    process.setProgram(command.program);
    process.setArguments(command.arguments);
    process.setProcessChannelMode(QProcess::ForwardedChannels);
    process.setStandardInputFile(QProcess::nullDevice());
    process.setChildProcessModifier([] {
        (void)::setsid();
    });
    process.start();

    if (!process.waitForStarted(5000)) {
        err << "kriscc-admin: avvio fallito per " << command.program
            << ": " << process.errorString() << '\n';
        reportStatus(err, QStringLiteral("failed-to-start"));
        return 125;
    }

    const qint64 pgid = process.processId();
    ProcessMap tracked;
    refreshTrackedGroup(pgid, &tracked, true);

    int remaining = command.timeoutMs;
    bool leaderFinished = false;
    int leaderExitCode = 125;
    QProcess::ExitStatus leaderExitStatus = QProcess::CrashExit;

    while (remaining > 0) {
        const int slice = qMin(100, remaining);
        const bool finished = process.waitForFinished(slice);
        remaining -= slice;
        refreshTrackedGroup(pgid, &tracked, false);
        if (finished) {
            leaderFinished = true;
            leaderExitCode = process.exitCode();
            leaderExitStatus = process.exitStatus();
            break;
        }
    }

    if (leaderFinished) {
        // A successful leader is not enough: privileged descendants remain part
        // of the operation. Give a short natural-drain window while preserving
        // observed process identities to avoid signalling a reused numeric PGID.
        for (int i = 0; i < 5 && trackedGroupAlive(pgid, tracked); ++i) {
            ::usleep(100 * 1000);
            refreshTrackedGroup(pgid, &tracked, false);
        }
        if (!trackedGroupAlive(pgid, tracked)) {
            if (leaderExitStatus != QProcess::NormalExit) {
                err << "kriscc-admin: processo terminato in modo anomalo: "
                    << command.program << '\n';
                reportStatus(err, QStringLiteral("crashed"));
                return 125;
            }
            reportStatus(err, QStringLiteral("child %1").arg(leaderExitCode));
            return leaderExitCode;
        }

        err << "kriscc-admin: il comando ha lasciato processi discendenti attivi: "
            << command.program << '\n';
        err.flush();
        (void)signalTrackedGroup(pgid, &tracked, SIGTERM);
        for (int i = 0; i < 30 && trackedGroupAlive(pgid, tracked); ++i) {
            ::usleep(100 * 1000);
            refreshTrackedGroup(pgid, &tracked, false);
        }
        if (trackedGroupAlive(pgid, tracked))
            (void)signalTrackedGroup(pgid, &tracked, SIGKILL);
        reportStatus(err, QStringLiteral("descendants-alive"));
        return 125;
    }

    err << "kriscc-admin: timeout per " << command.program << '\n';
    err.flush();

    if (!signalTrackedGroup(pgid, &tracked, SIGTERM, true))
        process.terminate();

    for (int i = 0; i < 30; ++i) {
        (void)process.waitForFinished(100);
        refreshTrackedGroup(pgid, &tracked, false);
        if (process.state() == QProcess::NotRunning && !trackedGroupAlive(pgid, tracked))
            break;
    }

    if (trackedGroupAlive(pgid, tracked))
        (void)signalTrackedGroup(pgid, &tracked, SIGKILL);
    if (process.state() != QProcess::NotRunning) {
        process.kill();
        process.waitForFinished(3000);
    }
    reportStatus(err, QStringLiteral("timeout"));
    return 124;
}

}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QTextStream err(stderr);

    if (::geteuid() != 0) {
        err << "kriscc-admin: l'helper deve essere eseguito come root tramite Polkit.\n";
        return 77;
    }

    const QStringList request = QCoreApplication::arguments().mid(1);
    const auto command = AdminPolicy::resolve(request);
    if (!command) {
        err << "kriscc-admin: operazione o argomenti non consentiti.\n";
        return 64;
    }
    return runProgram(*command);
}
