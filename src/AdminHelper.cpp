#include "Validators.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QProcess>
#include <QStringList>
#include <QTextStream>

#include <signal.h>
#include <unistd.h>

namespace {

struct Command {
    QString program;
    QStringList arguments;
    int timeoutMs = 0;
};

bool buildCommand(const QStringList &args, Command *command)
{
    if (!command || args.size() < 2)
        return false;

    const QString operation = args.at(1);

    if (operation == QStringLiteral("bootc-check") && args.size() == 2) {
        *command = {QStringLiteral("/usr/bin/bootc"),
                    {QStringLiteral("upgrade"), QStringLiteral("--check")},
                    30 * 60 * 1000};
        return true;
    }
    if (operation == QStringLiteral("bootc-download") && args.size() == 2) {
        *command = {QStringLiteral("/usr/bin/bootc"),
                    {QStringLiteral("upgrade"), QStringLiteral("--download-only")},
                    30 * 60 * 1000};
        return true;
    }
    if (operation == QStringLiteral("bootc-prepare") && args.size() == 2) {
        *command = {QStringLiteral("/usr/bin/bootc"),
                    {QStringLiteral("upgrade")},
                    30 * 60 * 1000};
        return true;
    }
    if (operation == QStringLiteral("bootc-apply-downloaded") && args.size() == 2) {
        *command = {QStringLiteral("/usr/bin/bootc"),
                    {QStringLiteral("upgrade"), QStringLiteral("--from-downloaded"),
                     QStringLiteral("--apply")},
                    30 * 60 * 1000};
        return true;
    }

    if (operation == QStringLiteral("rk-sync") && args.size() == 2) {
        *command = {QStringLiteral("/usr/bin/rk"), {QStringLiteral("sync")}, 30 * 60 * 1000};
        return true;
    }
    if ((operation == QStringLiteral("rk-add")
         || operation == QStringLiteral("rk-rm")
         || operation == QStringLiteral("rk-forget"))
        && args.size() == 3 && Validators::packageName(args.at(2))) {
        const QString rkOperation = operation == QStringLiteral("rk-add")
            ? QStringLiteral("add")
            : operation == QStringLiteral("rk-rm")
                ? QStringLiteral("rm") : QStringLiteral("forget");
        *command = {QStringLiteral("/usr/bin/rk"), {rkOperation, args.at(2)}, 30 * 60 * 1000};
        return true;
    }

    if ((operation == QStringLiteral("repo-enable")
         || operation == QStringLiteral("repo-disable"))
        && args.size() == 3 && Validators::repositoryId(args.at(2))) {
        *command = {
            QStringLiteral("/usr/bin/dnf5"),
            {QStringLiteral("config-manager"),
             operation == QStringLiteral("repo-enable")
                 ? QStringLiteral("enable") : QStringLiteral("disable"),
             args.at(2)},
            5 * 60 * 1000
        };
        return true;
    }

    if (operation == QStringLiteral("repo-add") && args.size() == 3
        && Validators::repositoryUrl(args.at(2))) {
        *command = {
            QStringLiteral("/usr/bin/dnf5"),
            {QStringLiteral("config-manager"), QStringLiteral("addrepo"),
             QStringLiteral("--from-repofile=") + args.at(2)},
            5 * 60 * 1000
        };
        return true;
    }

    if (operation == QStringLiteral("boot-next-uefi") && args.size() == 3
        && Validators::bootToken(args.at(2))) {
        *command = {QStringLiteral("/usr/bin/efibootmgr"),
                    {QStringLiteral("-n"), args.at(2).toUpper()},
                    2 * 60 * 1000};
        return true;
    }

    if (operation == QStringLiteral("boot-next-grub") && args.size() == 3
        && Validators::grubEntry(args.at(2))) {
        *command = {QStringLiteral("/usr/bin/grub2-reboot"), {args.at(2)}, 2 * 60 * 1000};
        return true;
    }

    return false;
}

void forwardOutput(QProcess &process)
{
    const QByteArray data = process.readAllStandardOutput();
    if (data.isEmpty())
        return;
    fwrite(data.constData(), 1, size_t(data.size()), stdout);
    fflush(stdout);
}

int runCommand(const Command &command)
{
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.setStandardInputFile(QProcess::nullDevice());
    process.setChildProcessModifier([] {
        (void)::setsid();
    });

    process.start(command.program, command.arguments);
    if (!process.waitForStarted(5000)) {
        QTextStream(stderr) << "kriscc-admin: avvio fallito per "
                            << command.program << ": " << process.errorString() << '\n';
        return 69;
    }

    QElapsedTimer timer;
    timer.start();

    while (process.state() != QProcess::NotRunning) {
        process.waitForReadyRead(200);
        forwardOutput(process);

        if (timer.elapsed() < command.timeoutMs)
            continue;

        QTextStream(stderr) << "kriscc-admin: timeout dell'operazione privilegiata.\n";
        const qint64 pid = process.processId();
        if (pid > 0)
            (void)::kill(-pid, SIGTERM);
        if (!process.waitForFinished(3000)) {
            if (pid > 0)
                (void)::kill(-pid, SIGKILL);
            process.waitForFinished(3000);
        }
        forwardOutput(process);
        return 124;
    }

    forwardOutput(process);
    if (process.exitStatus() != QProcess::NormalExit)
        return 70;

    const int code = process.exitCode();
    if (code == 126 || code == 127) {
        QTextStream(stderr) << "kriscc-admin: il comando figlio è terminato con codice riservato "
                            << code << ".\n";
        return 70;
    }
    return code;
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

    Command command;
    if (!buildCommand(QCoreApplication::arguments(), &command)) {
        err << "kriscc-admin: operazione o argomenti non consentiti.\n";
        return 64;
    }

    return runCommand(command);
}
