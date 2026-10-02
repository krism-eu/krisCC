#include "CustomActionsBackend.h"

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
