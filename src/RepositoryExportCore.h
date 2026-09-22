#pragma once
#include <QByteArray>
#include <QString>

namespace RepositoryExportCore {
struct GreenRun {
    QString branch;
    QString sha;
    bool valid() const { return !branch.isEmpty() && !sha.isEmpty(); }
};
QString repositorySlug(const QString &repository);
QString workflowName(const QString &repository);
QString safeFileComponent(const QString &value);
GreenRun parseLatestGreenRun(const QByteArray &json, const QString &repository,
                             QString *error = nullptr);
bool writeCombinedRepository(const QString &rootPath, const QString &repository,
                             const QString &branch, const QString &commitSha,
                             const QString &destination,
                             int *textFiles = nullptr, int *binaryFiles = nullptr,
                             QString *error = nullptr);
}
