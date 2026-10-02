#include "CustomActionsBackend.h"
#include "BashPromptConfig.h"

#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

namespace {
void writeFile(const QString &path, const QByteArray &data)
{
    QFile file(path);
    QVERIFY2(file.open(QIODevice::WriteOnly), qPrintable(file.errorString()));
    QCOMPARE(file.write(data), data.size());
    file.close();
}
}

class CustomActionsTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
    }

    void combinesTextFilesInDeterministicOrder()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        const QString source = QDir(temp.path()).filePath(QStringLiteral("raccolta"));
        QVERIFY(QDir().mkpath(source));

        writeFile(QDir(source).filePath(QStringLiteral("b.txt")), QByteArrayLiteral("secondo"));
        writeFile(QDir(source).filePath(QStringLiteral("a.txt")), QByteArrayLiteral("primo\n"));

        CustomActionsBackend backend;
        const QString result =
            backend.combineTextFiles(QUrl::fromLocalFile(source));

        QCOMPARE(result, QDir(temp.path()).filePath(QStringLiteral("raccolta-contenuto.txt")));

        QFile output(result);
        QVERIFY(output.open(QIODevice::ReadOnly));
        QCOMPARE(output.readAll(),
                 QByteArrayLiteral("===== a.txt =====\n"
                                   "primo\n"
                                   "\n"
                                   "===== b.txt =====\n"
                                   "secondo\n"
                                   "\n"));
    }

    void skipsBinarySymlinkAndSubdirectory()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        const QString source = QDir(temp.path()).filePath(QStringLiteral("input"));
        QVERIFY(QDir().mkpath(source));
        QVERIFY(QDir().mkpath(QDir(source).filePath(QStringLiteral("subdir"))));

        const QString textPath = QDir(source).filePath(QStringLiteral("testo.txt"));
        writeFile(textPath, QByteArrayLiteral("leggibile"));
        writeFile(QDir(source).filePath(QStringLiteral("binario.dat")),
                  QByteArray("abc\0def", 7));
        writeFile(QDir(source).filePath(QStringLiteral("subdir/nascosto.txt")),
                  QByteArrayLiteral("non includere"));

        const QString linkPath = QDir(source).filePath(QStringLiteral("link.txt"));
        QVERIFY(QFile::link(textPath, linkPath));

        CustomActionsBackend backend;
        const QString result =
            backend.combineTextFiles(QUrl::fromLocalFile(source));
        QVERIFY(!result.isEmpty());

        QFile output(result);
        QVERIFY(output.open(QIODevice::ReadOnly));
        const QByteArray data = output.readAll();

        QVERIFY(data.contains("===== testo.txt ====="));
        QVERIFY(!data.contains("binario.dat"));
        QVERIFY(!data.contains("link.txt"));
        QVERIFY(!data.contains("nascosto.txt"));
    }

    void storesAndReloadsActionIcon()
    {
        const QString configFile =
            QDir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation))
                .filePath(QStringLiteral("custom-actions.json"));
        QFile::remove(configFile);

        {
            CustomActionsBackend backend;
            QVERIFY(backend.saveAction(QString(), QStringLiteral("Dischi"),
                                       QStringLiteral("Test icona"),
                                       QStringLiteral("printf ok"), true,
                                       QStringLiteral("drive-harddisk")));
            QCOMPARE(backend.actions().size(), 1);
            QCOMPARE(backend.actions().first().toMap().value(QStringLiteral("icon")).toString(),
                     QStringLiteral("drive-harddisk"));
        }

        {
            CustomActionsBackend reloaded;
            QCOMPARE(reloaded.actions().size(), 1);
            QCOMPARE(reloaded.actions().first().toMap().value(QStringLiteral("icon")).toString(),
                     QStringLiteral("drive-harddisk"));
        }

        QFile::remove(configFile);
    }

    void invalidActionIconFallsBackSafely()
    {
        const QString configFile =
            QDir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation))
                .filePath(QStringLiteral("custom-actions.json"));
        QFile::remove(configFile);

        CustomActionsBackend backend;
        QVERIFY(backend.saveAction(QString(), QStringLiteral("Fallback"),
                                   QString(), QStringLiteral("true"), false,
                                   QStringLiteral("../../icona-arbitraria")));
        QCOMPARE(backend.actions().size(), 1);
        QCOMPARE(backend.actions().first().toMap().value(QStringLiteral("icon")).toString(),
                 QStringLiteral("utilities-terminal"));

        QFile::remove(configFile);
    }

    void bashPromptAddsManagedBlockWithoutReplacingExistingContent()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        const QString path = QDir(temp.path()).filePath(QStringLiteral(".bashrc"));
        writeFile(path, QByteArrayLiteral("export KEEP_ME=1\n"));

        QString error;
        QVERIFY2(BashPromptConfig::applyPreset(path, QStringLiteral("readable"), &error),
                 qPrintable(error));
        QCOMPARE(BashPromptConfig::inspect(path), BashPromptConfig::Status::Managed);

        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray data = file.readAll();

        QVERIFY(data.contains("export KEEP_ME=1"));
        QVERIFY(data.contains("# >>> krisCC prompt >>>"));
        QVERIFY(data.contains("# <<< krisCC prompt <<<"));
        QVERIFY(data.contains("\\u@\\h"));
        QVERIFY(data.contains("\\w"));
        QVERIFY(!data.contains("\\\\u@\\\\h"));
        QVERIFY(!data.contains("\\\\w"));
    }

    void bashPromptReplacesExistingManagedBlock()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        const QString path = QDir(temp.path()).filePath(QStringLiteral(".bashrc"));
        writeFile(path, QByteArrayLiteral("export KEEP_ME=1\n"));

        QString error;
        QVERIFY(BashPromptConfig::applyPreset(path, QStringLiteral("readable"), &error));
        QVERIFY(BashPromptConfig::applyPreset(path, QStringLiteral("compact"), &error));

        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray data = file.readAll();

        QCOMPARE(data.count("# >>> krisCC prompt >>>"), 1);
        QCOMPARE(data.count("# <<< krisCC prompt <<<"), 1);
        QVERIFY(data.contains("export KEEP_ME=1"));
    }

    void bashPromptRejectsBrokenMarkersWithoutWriting()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        const QString path = QDir(temp.path()).filePath(QStringLiteral(".bashrc"));
        const QByteArray original =
            QByteArrayLiteral("export KEEP_ME=1\n# >>> krisCC prompt >>>\n");
        writeFile(path, original);

        QString error;
        QVERIFY(!BashPromptConfig::applyPreset(path, QStringLiteral("readable"), &error));
        QCOMPARE(BashPromptConfig::inspect(path), BashPromptConfig::Status::InvalidMarkers);

        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), original);
    }

    void bashPromptResetRemovesOnlyManagedBlock()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        const QString path = QDir(temp.path()).filePath(QStringLiteral(".bashrc"));
        writeFile(path, QByteArrayLiteral("export KEEP_ME=1\n"));

        QString error;
        QVERIFY(BashPromptConfig::applyPreset(path, QStringLiteral("minimal"), &error));
        QVERIFY(BashPromptConfig::removeManagedBlock(path, &error));
        QCOMPARE(BashPromptConfig::inspect(path), BashPromptConfig::Status::Unmanaged);

        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray data = file.readAll();

        QVERIFY(data.contains("export KEEP_ME=1"));
        QVERIFY(!data.contains("# >>> krisCC prompt >>>"));
        QVERIFY(!data.contains("# <<< krisCC prompt <<<"));
    }

    void bashPromptRejectsSymlink()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        const QString target = QDir(temp.path()).filePath(QStringLiteral("real-bashrc"));
        const QString link = QDir(temp.path()).filePath(QStringLiteral(".bashrc"));
        writeFile(target, QByteArrayLiteral("export KEEP_ME=1\n"));
        QVERIFY(QFile::link(target, link));

        QString error;
        QVERIFY(!BashPromptConfig::applyPreset(link, QStringLiteral("readable"), &error));
        QCOMPARE(BashPromptConfig::inspect(link), BashPromptConfig::Status::Symlink);

        QFile file(target);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArrayLiteral("export KEEP_ME=1\n"));
    }

    void bashPromptRejectsUnknownPreset()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        const QString path = QDir(temp.path()).filePath(QStringLiteral(".bashrc"));
        writeFile(path, QByteArrayLiteral("export KEEP_ME=1\n"));

        QString error;
        QVERIFY(!BashPromptConfig::applyPreset(path, QStringLiteral("arbitrary"), &error));

        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArrayLiteral("export KEEP_ME=1\n"));
    }

    void refusesExistingDestination()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        const QString source = QDir(temp.path()).filePath(QStringLiteral("documenti"));
        QVERIFY(QDir().mkpath(source));
        writeFile(QDir(source).filePath(QStringLiteral("a.txt")), QByteArrayLiteral("nuovo"));

        const QString destination =
            QDir(temp.path()).filePath(QStringLiteral("documenti-contenuto.txt"));
        writeFile(destination, QByteArrayLiteral("da non toccare"));

        CustomActionsBackend backend;
        QVERIFY(backend.combineTextFiles(QUrl::fromLocalFile(source)).isEmpty());
        QVERIFY(backend.errorText().contains(QStringLiteral("esiste già")));

        QFile output(destination);
        QVERIFY(output.open(QIODevice::ReadOnly));
        QCOMPARE(output.readAll(), QByteArrayLiteral("da non toccare"));
    }
};

QTEST_MAIN(CustomActionsTest)
#include "test_custom_actions.moc"
