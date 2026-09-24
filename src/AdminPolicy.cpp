#include "AdminPolicy.h"

#include "Validators.h"

namespace {
constexpr int kShortTimeoutMs = 2 * 60 * 1000;
constexpr int kRepositoryTimeoutMs = 5 * 60 * 1000;
constexpr int kLongTimeoutMs = 30 * 60 * 1000;
}

std::optional<AdminPolicy::Command> AdminPolicy::resolve(const QStringList &request)
{
    if (request.isEmpty())
        return std::nullopt;

    const QString &operation = request.at(0);

    if (request.size() == 1) {
        if (operation == QStringLiteral("bootc-check"))
            return Command{QStringLiteral("/usr/bin/bootc"),
                           {QStringLiteral("upgrade"), QStringLiteral("--check")},
                           kLongTimeoutMs};
        if (operation == QStringLiteral("bootc-download"))
            return Command{QStringLiteral("/usr/bin/bootc"),
                           {QStringLiteral("upgrade"), QStringLiteral("--download-only")},
                           kLongTimeoutMs};
        if (operation == QStringLiteral("bootc-prepare"))
            return Command{QStringLiteral("/usr/bin/bootc"),
                           {QStringLiteral("upgrade")}, kLongTimeoutMs};
        if (operation == QStringLiteral("bootc-apply-downloaded"))
            return Command{QStringLiteral("/usr/bin/bootc"),
                           {QStringLiteral("upgrade"), QStringLiteral("--from-downloaded"),
                            QStringLiteral("--apply")}, kLongTimeoutMs};
        if (operation == QStringLiteral("rk-sync"))
            return Command{QStringLiteral("/usr/bin/rk"),
                           {QStringLiteral("sync")}, kLongTimeoutMs};
        if (operation == QStringLiteral("journal-vacuum"))
            return Command{QStringLiteral("/usr/bin/journalctl"),
                           {QStringLiteral("--vacuum-size=100M")}, kShortTimeoutMs};
        if (operation == QStringLiteral("dnf-clean"))
            return Command{QStringLiteral("/usr/bin/dnf5"),
                           {QStringLiteral("clean"), QStringLiteral("all")}, kRepositoryTimeoutMs};
        return std::nullopt;
    }

    if (request.size() != 2)
        return std::nullopt;

    const QString &value = request.at(1);
    if ((operation == QStringLiteral("rk-add")
         || operation == QStringLiteral("rk-rm")
         || operation == QStringLiteral("rk-forget"))
        && Validators::packageName(value)) {
        const QString verb = operation == QStringLiteral("rk-add") ? QStringLiteral("add")
                           : operation == QStringLiteral("rk-rm") ? QStringLiteral("rm")
                                                                 : QStringLiteral("forget");
        return Command{QStringLiteral("/usr/bin/rk"), {verb, value}, kLongTimeoutMs};
    }

    if ((operation == QStringLiteral("repo-enable")
         || operation == QStringLiteral("repo-disable"))
        && Validators::repositoryId(value)) {
        return Command{QStringLiteral("/usr/bin/dnf5"),
                       {QStringLiteral("config-manager"),
                        operation == QStringLiteral("repo-enable")
                            ? QStringLiteral("enable") : QStringLiteral("disable"),
                        value}, kRepositoryTimeoutMs};
    }

    if (operation == QStringLiteral("repo-add") && Validators::repositoryUrl(value)) {
        return Command{QStringLiteral("/usr/bin/dnf5"),
                       {QStringLiteral("config-manager"), QStringLiteral("addrepo"),
                        QStringLiteral("--from-repofile=") + value},
                       kRepositoryTimeoutMs};
    }

    if (operation == QStringLiteral("boot-next-uefi") && Validators::bootToken(value)) {
        return Command{QStringLiteral("/usr/bin/efibootmgr"),
                       {QStringLiteral("-n"), value.toUpper()}, kShortTimeoutMs};
    }

    if (operation == QStringLiteral("boot-next-grub") && Validators::grubEntry(value)) {
        return Command{QStringLiteral("/usr/bin/grub2-reboot"),
                       {value}, kShortTimeoutMs};
    }

    return std::nullopt;
}
