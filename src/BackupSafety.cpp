#include "BackupSafety.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QVariantMap>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <linux/fs.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace {
class ScopedFd
{
public:
    ScopedFd() = default;
    explicit ScopedFd(int fd) : m_fd(fd) {}
    ~ScopedFd() { if (m_fd >= 0) ::close(m_fd); }
    ScopedFd(const ScopedFd &) = delete;
    ScopedFd &operator=(const ScopedFd &) = delete;
    ScopedFd(ScopedFd &&other) noexcept : m_fd(other.release()) {}
    ScopedFd &operator=(ScopedFd &&other) noexcept
    {
        if (this != &other) {
            if (m_fd >= 0)
                ::close(m_fd);
            m_fd = other.release();
        }
        return *this;
    }
    int get() const { return m_fd; }
    bool valid() const { return m_fd >= 0; }
    int release() { const int fd = m_fd; m_fd = -1; return fd; }
private:
    int m_fd = -1;
};

QString errnoMessage(const QString &prefix)
{
    return prefix + QStringLiteral(": ") + QString::fromLocal8Bit(std::strerror(errno));
}

ScopedFd openDirectoryNoLinks(const QString &path, QString *error)
{
    const QString clean = QDir::cleanPath(path);
    if (!QDir::isAbsolutePath(clean)) {
        if (error)
            *error = QStringLiteral("La cartella backup non è un percorso assoluto.");
        return {};
    }

    ScopedFd current(::open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC));
    if (!current.valid()) {
        if (error)
            *error = errnoMessage(QStringLiteral("Impossibile aprire la radice filesystem"));
        return {};
    }

    const QStringList components = clean.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (const QString &component : components) {
        if (component == QStringLiteral(".") || component == QStringLiteral("..")
            || component.contains(QLatin1Char('/'))) {
            if (error)
                *error = QStringLiteral("Componente non valido nel percorso backup.");
            return {};
        }
        const QByteArray encoded = QFile::encodeName(component);
        const int next = ::openat(current.get(), encoded.constData(),
                                  O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        if (next < 0) {
            if (error)
                *error = errnoMessage(QStringLiteral("Percorso backup non attraversabile in sicurezza"));
            return {};
        }
        current = ScopedFd(next);
    }
    return current;
}

bool requestedLeaf(const QString &root, const QString &requestedPath,
                   QString *name, QString *error)
{
    const QString cleanRoot = QDir::cleanPath(root);
    const QString cleanPath = QDir::cleanPath(requestedPath);
    if (!QDir::isAbsolutePath(cleanPath)) {
        if (error)
            *error = QStringLiteral("Il percorso del backup non è assoluto.");
        return false;
    }

    const QFileInfo info(cleanPath);
    if (QDir::cleanPath(info.absolutePath()) != cleanRoot
        || !BackupSafety::validBackupName(info.fileName())) {
        if (error)
            *error = QStringLiteral("Archivio non ammesso nella cartella backup selezionata.");
        return false;
    }
    if (name)
        *name = info.fileName();
    return true;
}

bool sameIdentity(const struct stat &a, const struct stat &b)
{
    return a.st_dev == b.st_dev && a.st_ino == b.st_ino;
}

bool sameStableMetadata(const struct stat &a, const struct stat &b)
{
    return sameIdentity(a, b)
        && a.st_size == b.st_size
        && a.st_mtim.tv_sec == b.st_mtim.tv_sec
        && a.st_mtim.tv_nsec == b.st_mtim.tv_nsec
        && a.st_ctim.tv_sec == b.st_ctim.tv_sec
        && a.st_ctim.tv_nsec == b.st_ctim.tv_nsec;
}

int renameNoReplace(int dirfd, const QByteArray &from, const QByteArray &to)
{
#ifdef SYS_renameat2
    return int(::syscall(SYS_renameat2, dirfd, from.constData(), dirfd, to.constData(),
                         RENAME_NOREPLACE));
#else
    Q_UNUSED(dirfd)
    Q_UNUSED(from)
    Q_UNUSED(to)
    errno = ENOSYS;
    return -1;
#endif
}

bool writeAll(int fd, const char *data, qsizetype size)
{
    qsizetype offset = 0;
    while (offset < size) {
        const ssize_t written = ::write(fd, data + offset, size_t(size - offset));
        if (written < 0) {
            if (errno == EINTR)
                continue;
            return false;
        }
        if (written == 0) {
            errno = EIO;
            return false;
        }
        offset += qsizetype(written);
    }
    return true;
}
}

namespace BackupSafety {

bool validBackupName(const QString &name)
{
    static const QRegularExpression pattern(
        QStringLiteral("^(config|home)-[0-9]{8}-[0-9]{6}\\.tar\\.gz$"));
    return pattern.match(name).hasMatch();
}

QString backupKind(const QString &name)
{
    if (!validBackupName(name))
        return {};
    return name.startsWith(QStringLiteral("home-"))
        ? QStringLiteral("home") : QStringLiteral("config");
}

QVariantList listBackups(const QString &backupRoot, QString *error)
{
    QVariantList result;
    ScopedFd root = openDirectoryNoLinks(backupRoot, error);
    if (!root.valid())
        return result;

    DIR *directory = ::fdopendir(::dup(root.get()));
    if (!directory) {
        if (error)
            *error = errnoMessage(QStringLiteral("Impossibile elencare la cartella backup"));
        return result;
    }

    errno = 0;
    while (dirent *entry = ::readdir(directory)) {
        const QString name = QFile::decodeName(entry->d_name);
        if (!validBackupName(name))
            continue;

        struct stat st {};
        if (::fstatat(root.get(), entry->d_name, &st, AT_SYMLINK_NOFOLLOW) != 0
            || !S_ISREG(st.st_mode)) {
            continue;
        }

        ScopedFd archive(::openat(root.get(), entry->d_name,
                                  O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
        if (!archive.valid())
            continue;
        struct stat opened {};
        if (::fstat(archive.get(), &opened) != 0 || !sameIdentity(st, opened)
            || !S_ISREG(opened.st_mode)) {
            continue;
        }

        QVariantMap item;
        item.insert(QStringLiteral("name"), name);
        item.insert(QStringLiteral("path"), QDir(backupRoot).filePath(name));
        item.insert(QStringLiteral("size"), qint64(opened.st_size));
        item.insert(QStringLiteral("modified"),
                    QDateTime::fromSecsSinceEpoch(opened.st_mtim.tv_sec).toString(Qt::ISODate));
        item.insert(QStringLiteral("kind"), backupKind(name));
        result.append(item);
    }
    const int readError = errno;
    ::closedir(directory);
    if (readError != 0 && error) {
        errno = readError;
        *error = errnoMessage(QStringLiteral("Errore durante l'elenco dei backup"));
    }

    std::sort(result.begin(), result.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value(QStringLiteral("modified")).toString()
            > b.toMap().value(QStringLiteral("modified")).toString();
    });
    return result;
}

bool removeBackup(const QString &backupRoot, const QString &requestedPath,
                  QString *removedName, QString *error)
{
    QString name;
    if (!requestedLeaf(backupRoot, requestedPath, &name, error))
        return false;

    ScopedFd root = openDirectoryNoLinks(backupRoot, error);
    if (!root.valid())
        return false;

    const QByteArray encoded = QFile::encodeName(name);
    ScopedFd archive(::openat(root.get(), encoded.constData(),
                              O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
    if (!archive.valid()) {
        if (error)
            *error = errnoMessage(QStringLiteral("Il backup non è un file regolare leggibile"));
        return false;
    }

    struct stat opened {};
    if (::fstat(archive.get(), &opened) != 0 || !S_ISREG(opened.st_mode)) {
        if (error)
            *error = QStringLiteral("Il backup selezionato non è un file regolare.");
        return false;
    }

    struct stat named {};
    if (::fstatat(root.get(), encoded.constData(), &named, AT_SYMLINK_NOFOLLOW) != 0
        || !S_ISREG(named.st_mode) || !sameIdentity(opened, named)) {
        if (error)
            *error = QStringLiteral("Il backup è cambiato prima della cancellazione.");
        return false;
    }

    const QString quarantineName = QStringLiteral(".kriscc-delete-%1-%2")
        .arg(QCoreApplication::applicationPid())
        .arg(QRandomGenerator::global()->generate64(), 0, 16);
    const QByteArray quarantine = QFile::encodeName(quarantineName);

    if (renameNoReplace(root.get(), encoded, quarantine) != 0) {
        if (error) {
            if (errno == ENOSYS)
                *error = QStringLiteral("renameat2 non è disponibile: cancellazione sicura rifiutata.");
            else
                *error = errnoMessage(QStringLiteral("Impossibile isolare il backup prima della cancellazione"));
        }
        return false;
    }

    struct stat quarantined {};
    if (::fstatat(root.get(), quarantine.constData(), &quarantined, AT_SYMLINK_NOFOLLOW) != 0
        || !S_ISREG(quarantined.st_mode) || !sameIdentity(opened, quarantined)) {
        (void)renameNoReplace(root.get(), quarantine, encoded);
        if (error)
            *error = QStringLiteral("Il backup è stato sostituito durante la cancellazione; nessun oggetto è stato eliminato.");
        return false;
    }

    if (::unlinkat(root.get(), quarantine.constData(), 0) != 0) {
        (void)renameNoReplace(root.get(), quarantine, encoded);
        if (error)
            *error = errnoMessage(QStringLiteral("Impossibile eliminare il backup isolato"));
        return false;
    }

    if (removedName)
        *removedName = name;
    return true;
}

bool makeStableArchiveCopy(const QString &backupRoot, const QString &requestedPath,
                           const QString &stagingDirectory, QString *stablePath,
                           QString *displayName, QString *error)
{
    QString name;
    if (!requestedLeaf(backupRoot, requestedPath, &name, error))
        return false;

    ScopedFd root = openDirectoryNoLinks(backupRoot, error);
    if (!root.valid())
        return false;
    ScopedFd staging = openDirectoryNoLinks(stagingDirectory, error);
    if (!staging.valid())
        return false;

    const QByteArray encoded = QFile::encodeName(name);
    ScopedFd source(::openat(root.get(), encoded.constData(),
                             O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
    if (!source.valid()) {
        if (error)
            *error = errnoMessage(QStringLiteral("Impossibile aprire il backup senza seguire collegamenti"));
        return false;
    }

    struct stat before {};
    if (::fstat(source.get(), &before) != 0 || !S_ISREG(before.st_mode)) {
        if (error)
            *error = QStringLiteral("Il backup selezionato non è un file regolare.");
        return false;
    }

    const QByteArray stableName("archive.tar.gz");
    ScopedFd destination(::openat(staging.get(), stableName.constData(),
                                  O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW,
                                  S_IRUSR | S_IWUSR));
    if (!destination.valid()) {
        if (error)
            *error = errnoMessage(QStringLiteral("Impossibile creare la copia privata del backup"));
        return false;
    }

    QByteArray buffer(256 * 1024, Qt::Uninitialized);
    for (;;) {
        const ssize_t count = ::read(source.get(), buffer.data(), size_t(buffer.size()));
        if (count < 0) {
            if (errno == EINTR)
                continue;
            if (error)
                *error = errnoMessage(QStringLiteral("Errore leggendo il backup"));
            return false;
        }
        if (count == 0)
            break;
        if (!writeAll(destination.get(), buffer.constData(), qsizetype(count))) {
            if (error)
                *error = errnoMessage(QStringLiteral("Errore scrivendo la copia privata del backup"));
            return false;
        }
    }

    if (::fsync(destination.get()) != 0) {
        if (error)
            *error = errnoMessage(QStringLiteral("Impossibile sincronizzare la copia privata del backup"));
        return false;
    }

    struct stat after {};
    if (::fstat(source.get(), &after) != 0 || !sameStableMetadata(before, after)) {
        if (error)
            *error = QStringLiteral("Il backup è stato modificato durante la copia; operazione annullata.");
        return false;
    }

    if (stablePath)
        *stablePath = QDir(stagingDirectory).filePath(QString::fromLatin1(stableName));
    if (displayName)
        *displayName = name;
    return true;
}

} // namespace BackupSafety
