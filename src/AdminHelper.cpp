#include "Validators.h"

#include <QCoreApplication>
#include <QProcess>
#include <QStringList>
#include <QTextStream>

#include <signal.h>
#include <unistd.h>

namespace {

constexpr int kShortTimeoutMs = 2 * 60 * 1000;
constexpr int kRepositoryTimeoutMs = 5 * 60 * 1000;
constexpr int kLongTimeoutMs = 30 * 60 * 1000;

int runProgram(const QString &program, const QStringList &arguments, int timeoutMs)
{
    QTextStream err(stderr);
    QProcess process;
    process.setProgram(program);
    process.setArguments(arguments);
    process.setProcessChannelMode(QProcess::ForwardedChannels);
    process.setStandardInputFile(QProcess::nullDevice());
    process.setChildProcessModifier([] {
        (void)::setsid();
    });
    process.start();

    if (!process.waitForStarted(5000)) {
        err << "kriscc-admin: avvio fallito per " << program
            << ": " << process.errorString() << '\n';
        return 125;
    }

    if (process.waitForFinished(timeoutMs)) {
        if (process.exitStatus() != QProcess::NormalExit) {
            err << "kriscc-admin: processo terminato in modo anomalo: " << program << '\n';
            return 125;
        }
        return process.exitCode();
    }

    const qint64 pid = process.processId();
    err << "kriscc-admin: timeout per " << program << '\n';
    err.flush();

    if (pid > 0)
        (void)::kill(-pid, SIGTERM);
    else
        process.terminate();

    if (!process.waitForFinished(3000)) {
        if (pid > 0)
            (void)::kill(-pid, SIGKILL);
        else
            process.kill();
        process.waitForFinished(3000);
    }
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

    const QStringList args = QCoreApplication::arguments();
    if (args.size() < 2) {
        err << "kriscc-admin: operazione mancante.\n";
        return 64;
    }

    const QString operation = args.at(1);

    if (operation == QStringLiteral("bootc-check") && args.size() == 2)
        return runProgram(QStringLiteral("/usr/bin/bootc"),
                          {QStringLiteral("upgrade"), QStringLiteral("--check")},
                          kLongTimeoutMs);
    if (operation == QStringLiteral("bootc-download") && args.size() == 2)
        return runProgram(QStringLiteral("/usr/bin/bootc"),
                          {QStringLiteral("upgrade"), QStringLiteral("--download-only")},
                          kLongTimeoutMs);
    if (operation == QStringLiteral("bootc-prepare") && args.size() == 2)
        return runProgram(QStringLiteral("/usr/bin/bootc"),
                          {QStringLiteral("upgrade")}, kLongTimeoutMs);
    if (operation == QStringLiteral("bootc-apply-downloaded") && args.size() == 2)
        return runProgram(QStringLiteral("/usr/bin/bootc"),
                          {QStringLiteral("upgrade"), QStringLiteral("--from-downloaded"),
                           QStringLiteral("--apply")}, kLongTimeoutMs);

    if (operation == QStringLiteral("rk-sync") && args.size() == 2)
        return runProgram(QStringLiteral("/usr/bin/rk"), {QStringLiteral("sync")}, kLongTimeoutMs);

    if ((operation == QStringLiteral("rk-add")
         || operation == QStringLiteral("rk-rm")
         || operation == QStringLiteral("rk-forget"))
        && args.size() == 3 && Validators::packageName(args.at(2))) {
        const QString verb = operation == QStringLiteral("rk-add") ? QStringLiteral("add")
                           : operation == QStringLiteral("rk-rm") ? QStringLiteral("rm")
                                                                 : QStringLiteral("forget");
        return runProgram(QStringLiteral("/usr/bin/rk"), {verb, args.at(2)}, kLongTimeoutMs);
    }

    if ((operation == QStringLiteral("repo-enable")
         || operation == QStringLiteral("repo-disable"))
        && args.size() == 3 && Validators::repositoryId(args.at(2))) {
        return runProgram(QStringLiteral("/usr/bin/dnf5"),
                          {QStringLiteral("config-manager"),
                           operation == QStringLiteral("repo-enable")
                               ? QStringLiteral("enable") : QStringLiteral("disable"),
                           args.at(2)}, kRepositoryTimeoutMs);
    }

    if (operation == QStringLiteral("repo-add") && args.size() == 3
        && Validators::repositoryUrl(args.at(2))) {
        return runProgram(QStringLiteral("/usr/bin/dnf5"),
                          {QStringLiteral("config-manager"), QStringLiteral("addrepo"),
                           QStringLiteral("--from-repofile=") + args.at(2)},
                          kRepositoryTimeoutMs);
    }

    if (operation == QStringLiteral("boot-next-uefi") && args.size() == 3
        && Validators::bootToken(args.at(2))) {
        return runProgram(QStringLiteral("/usr/bin/efibootmgr"),
                          {QStringLiteral("-n"), args.at(2).toUpper()}, kShortTimeoutMs);
    }

    if (operation == QStringLiteral("boot-next-grub") && args.size() == 3
        && Validators::grubEntry(args.at(2))) {
        return runProgram(QStringLiteral("/usr/bin/grub2-reboot"),
                          {args.at(2)}, kShortTimeoutMs);
    }

    err << "kriscc-admin: operazione o argomenti non consentiti.\n";
    return 64;
}
