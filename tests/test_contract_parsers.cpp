#include <QtTest>
#include "ContractParsers.h"

class ContractParsersTest final : public QObject
{
    Q_OBJECT
private slots:
    void rkStatus()
    {
        const QByteArray valid = "{\"mount\":\"66 overlay rw\",\"needs_sync\":false,\"overlay\":\"ready\",\"overlay_error\":\"\",\"pending_recovery\":false,\"requests\":[\"tree\"],\"schema\":1}";
        const auto parsed = ContractParsers::parseRkStatus(valid);
        QVERIFY(parsed.ok());
        QCOMPARE(parsed.overlay, QStringLiteral("ready"));
        QCOMPARE(parsed.requests, QStringList({QStringLiteral("tree")}));
        QVERIFY(!ContractParsers::parseRkStatus("{").ok());
        QVERIFY(!ContractParsers::parseRkStatus("{\"schema\":2,\"overlay\":\"ready\",\"pending_recovery\":false,\"needs_sync\":false,\"requests\":[]}").ok());
    }

    void bootcStatus()
    {
        const QByteArray valid = "{\"apiVersion\":\"org.containers.bootc/v1\",\"kind\":\"BootcHost\",\"status\":{\"booted\":{\"image\":{\"image\":{\"image\":\"ghcr.io/krism-eu/krisos:m1\"},\"imageDigest\":\"sha256:abc\",\"version\":\"0.1.0-m1\"}},\"rollback\":null,\"staged\":null}}";
        const auto parsed = ContractParsers::parseBootcStatus(valid);
        QVERIFY(parsed.ok());
        QCOMPARE(parsed.deployments.size(), 1);
        QCOMPARE(parsed.deployments.at(0).toMap().value(QStringLiteral("role")).toString(), QStringLiteral("Booted"));
        QVERIFY(!ContractParsers::parseBootcStatus("{\"apiVersion\":\"org.containers.bootc/v2\",\"kind\":\"BootcHost\",\"status\":{}}").ok());
    }

    void dnfFormats()
    {
        const auto repo = ContractParsers::parseDnfRepoquery("tree\tTree utility\t2.2.1-4.fc44\tfedora\tx86_64\t1234\t5678\n");
        QVERIFY(repo.ok());
        QCOMPARE(repo.values.size(), 1);
        QVERIFY(!ContractParsers::parseDnfRepoquery("tree\tbad\n").ok());

        const auto list = ContractParsers::parseDnfListJson("{\"installed\":[{\"name\":\"bash\",\"arch\":\"x86_64\",\"evr\":\"5.3-1.fc44\",\"repository\":\"@System\"}]}");
        QVERIFY(list.ok());
        QCOMPARE(list.values.size(), 1);
        QVERIFY(!ContractParsers::parseDnfListJson("{\"installed\":[{\"name\":\"bash\"}]}").ok());
    }

    void flatpakAndPodman()
    {
        const auto flatpak = ContractParsers::parseFlatpakTsv("Vivaldi\tcom.vivaldi.Vivaldi\t8.2\tflathub\n", 4);
        QVERIFY(flatpak.ok());
        QCOMPARE(flatpak.values.at(0).toStringList().at(1), QStringLiteral("com.vivaldi.Vivaldi"));

        const auto remoteThree = ContractParsers::parseFlatpakRemotes(
            "flathub\tFlathub\thttps://dl.flathub.org/repo/\n");
        QVERIFY(remoteThree.ok());
        QCOMPARE(remoteThree.values.size(), 1);
        QCOMPARE(remoteThree.values.at(0).toStringList().size(), 4);
        QCOMPARE(remoteThree.values.at(0).toStringList().at(3), QString());

        const auto remoteFour = ContractParsers::parseFlatpakRemotes(
            "flathub\tFlathub\thttps://dl.flathub.org/repo/\t\n");
        QVERIFY(remoteFour.ok());
        QVERIFY(!ContractParsers::parseFlatpakRemotes("broken\trow\n").ok());

        const auto remotes = ContractParsers::parseFlatpakTsv(
            "flathub\tFlathub\thttps://dl.flathub.org/repo/\t\n", 4);
        QVERIFY(remotes.ok());
        QCOMPARE(remotes.values.size(), 1);
        QCOMPARE(remotes.values.at(0).toStringList().size(), 4);
        QCOMPARE(remotes.values.at(0).toStringList().at(3), QString());

        const auto podman = ContractParsers::parsePodmanJson("[{\"Names\":[\"demo\"],\"State\":\"running\",\"Size\":{\"rootFsSize\":2048,\"rwSize\":512}}]");
        QVERIFY(podman.ok());
        QCOMPARE(podman.values.size(), 1);
        QVERIFY(!ContractParsers::parsePodmanJson("{\"not\":\"array\"}").ok());
    }

    void bootEntries()
    {
        const auto uefi = ContractParsers::parseUefiEntries("Boot0001* KrisOS\tHD(1,GPT,abcd,0x800,0x100000)/File(\\\\EFI\\\\fedora\\\\shimx64.efi)\n");
        QCOMPARE(uefi.values.size(), 1);
        QCOMPARE(uefi.values.at(0).toMap().value(QStringLiteral("label")).toString(), QStringLiteral("0001 · KrisOS"));

        const auto grub = ContractParsers::parseGrubbyEntries("index=0\ntitle=\"KrisOS\"\nid=\"ostree-1\"\n");
        QCOMPARE(grub.values.size(), 1);
        QCOMPARE(grub.values.at(0).toMap().value(QStringLiteral("id")).toString(), QStringLiteral("ostree-1"));
    }
};
QTEST_APPLESS_MAIN(ContractParsersTest)
#include "test_contract_parsers.moc"