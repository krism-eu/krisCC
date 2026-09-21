#include "AdminPolicy.h"

#include "Validators.h"

bool buildAdminCommand(const QStringList &args, AdminCommand *command)
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
        *command = {QStringLiteral("/usr/bin/rk"), {QStringLiteral("sync")},
                    30 * 60 * 1000};
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
        *command = {QStringLiteral("/usr/bin/rk"), {rkOperation, args.at(2)},
                    30 * 60 * 1000};
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
        *command = {QStringLiteral("/usr/bin/grub2-reboot"), {args.at(2)},
                    2 * 60 * 1000};
        return true;
    }

    return false;
}
