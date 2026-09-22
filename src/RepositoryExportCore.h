#pragma once
#include <QByteArray>
#include <QString>
#include <QStringList>

namespace RepositoryExportCore {
QString repositorySlug(const QString &repository);
QString safeFileComponent(const QString &value);
QStringList parseBranchList(const QByteArray &json, QString *error = nullptr);
QString parseCommitSha(const QByteArray &json, QString *error = nullptr);
bool writeCombinedRepository(const QString &rootPath, const QString &repository,
                             const QString &branch, const QString &commitSha,
                             const QString &destination,
                             int *textFiles = nullptr, int *binaryFiles = nullptr,
                             QString *error = nullptr);
}
