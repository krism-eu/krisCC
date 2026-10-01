#include "ArchiveRestoreEngine.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QLibrary>
#include <QRandomGenerator>
#include <QSet>
#include <QTemporaryDir>
#include <QVector>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <linux/openat2.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

struct archive;
struct archive_entry;

namespace {
constexpr int kArchiveOk = 0;
constexpr int kArchiveEof = 1;
constexpr int kMaxMembers = 100000;
constexpr qsizetype kMaxPathBytes = 16 * 1024;
constexpr quint64 kMaxSingleFileBytes = 256ULL * 1024ULL * 1024ULL * 1024ULL;
constexpr quint64 kMaxTotalFileBytes = 512ULL * 1024ULL * 1024ULL * 1024ULL;

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
    int release() { const int value = m_fd; m_fd = -1; return value; }
private:
    int m_fd = -1;
};

QString errnoText(const QString &context)
{
    return context + QStringLiteral(": ") + QString::fromLocal8Bit(std::strerror(errno));
}

class ArchiveApi
{
public:
    using NewFn = archive *(*)();
    using IntArchiveFn = int (*)(archive *);
    using OpenFilenameFn = int (*)(archive *, const char *, size_t);
    using NextHeaderFn = int (*)(archive *, archive_entry **);
    using DataBlockFn = int (*)(archive *, const void **, size_t *, qint64 *);
    using ErrorFn = const char *(*)(archive *);
    using TextEntryFn = const char *(*)(archive_entry *);
    using SizeEntryFn = qint64 (*)(archive_entry *);
    using ModeEntryFn = mode_t (*)(archive_entry *);
    using TimeEntryFn = qint64 (*)(archive_entry *);

    bool load(QString *error)
    {
        m_library.setFileName(QStringLiteral("archive"));
        if (!m_library.load()) {
            if (error)
                *error = QStringLiteral("libarchive non disponibile: %1").arg(m_library.errorString());
            return false;
        }

        bool ok = true;
        ok = resolve(archiveNew, "archive_read_new") && ok;
        ok = resolve(supportGzip, "archive_read_support_filter_gzip") && ok;
        ok = resolve(supportTar, "archive_read_support_format_tar") && ok;
        ok = resolve(openFilename, "archive_read_open_filename") && ok;
        ok = resolve(nextHeader, "archive_read_next_header") && ok;
        ok = resolve(dataBlock, "archive_read_data_block") && ok;
        ok = resolve(dataSkip, "archive_read_data_skip") && ok;
        ok = resolve(readFree, "archive_read_free") && ok;
        ok = resolve(errorString, "archive_error_string") && ok;
        ok = resolve(pathname, "archive_entry_pathname") && ok;
        ok = resolve(symlink, "archive_entry_symlink") && ok;
        ok = resolve(hardlink, "archive_entry_hardlink") && ok;
        ok = resolve(entrySize, "archive_entry_size") && ok;
        ok = resolve(entryMode, "archive_entry_mode") && ok;
        ok = resolve(entryMtime, "archive_entry_mtime") && ok;
        if (!ok && error)
            *error = QStringLiteral("libarchive non espone l'API richiesta.");
        return ok;
    }

    NewFn archiveNew = nullptr;
    IntArchiveFn supportGzip = nullptr;
    IntArchiveFn supportTar = nullptr;
    OpenFilenameFn openFilename = nullptr;
    NextHeaderFn nextHeader = nullptr;
    DataBlockFn dataBlock = nullptr;
    IntArchiveFn dataSkip = nullptr;
    IntArchiveFn readFree = nullptr;
    ErrorFn errorString = nullptr;
    TextEntryFn pathname = nullptr;
    TextEntryFn symlink = nullptr;
    TextEntryFn hardlink = nullptr;
    SizeEntryFn entrySize = nullptr;
    ModeEntryFn entryMode = nullptr;
    TimeEntryFn entryMtime = nullptr;

private:
    template <typename T>
    bool resolve(T &target, const char *name)
    {
        target = reinterpret_cast<T>(m_library.resolve(name));
        return target != nullptr;
    }

    QLibrary m_library;
};

class ArchiveReader
{
public:
    ArchiveReader(ArchiveApi *api, const QString &path, QString *error)
        : m_api(api)
    {
        m_archive = m_api->archiveNew();
        if (!m_archive) {
            if (error)
                *error = QStringLiteral("Impossibile inizializzare il lettore archivio.");
            return;
        }
        if (m_api->supportGzip(m_archive) != kArchiveOk
            || m_api->supportTar(m_archive) != kArchiveOk) {
            if (error)
                *error = QStringLiteral("Supporto tar/gzip non disponibile in libarchive.");
            m_api->readFree(m_archive);
            m_archive = nullptr;
            return;
        }
        const QByteArray encoded = QFile::encodeName(path);
        if (m_api->openFilename(m_archive, encoded.constData(), 64 * 1024) != kArchiveOk) {
            if (error)
                *error = archiveError(QStringLiteral("Impossibile aprire l'archivio"));
            m_api->readFree(m_archive);
            m_archive = nullptr;
        }
    }

    ~ArchiveReader()
    {
        if (m_archive)
            m_api->readFree(m_archive);
    }

    bool valid() const { return m_archive != nullptr; }
    int next(archive_entry **entry) { return m_api->nextHeader(m_archive, entry); }
    int skip() { return m_api->dataSkip(m_archive); }
    int dataBlock(const void **buffer, size_t *size, qint64 *offset)
    {
        return m_api->dataBlock(m_archive, buffer, size, offset);
    }
    QString archiveError(const QString &prefix) const
    {
        const char *raw = m_archive ? m_api->errorString(m_archive) : nullptr;
        const QString detail = raw ? QString::fromLocal8Bit(raw) : QString();
        return detail.isEmpty() ? prefix : prefix + QStringLiteral(": ") + detail;
    }

private:
    ArchiveApi *m_api = nullptr;
    archive *m_archive = nullptr;
};

enum class EntryKind { Regular, Directory, Symlink, Hardlink };

struct PlannedEntry
{
    QString path;
    EntryKind kind = EntryKind::Regular;
    QString linkTarget;
    QString resolvedHardTarget;
    quint64 size = 0;
    mode_t mode = 0600;
    qint64 mtime = 0;
};

struct ArchivePlan
{
    QVector<PlannedEntry> entries;
    QHash<QString, int> index;
    quint64 totalBytes = 0;
};

bool decodeArchiveText(const char *raw, QString *value, const QString &what, QString *error)
{
    if (!raw) {
        if (error)
            *error = QStringLiteral("%1 mancante nell'archivio.").arg(what);
        return false;
    }
    const QByteArray bytes(raw);
    if (bytes.size() > kMaxPathBytes) {
        if (error)
            *error = QStringLiteral("%1 troppo lungo nell'archivio.").arg(what);
        return false;
    }
    const QString decoded = QString::fromUtf8(bytes.constData(), bytes.size());
    if (decoded.toUtf8() != bytes) {
        if (error)
            *error = QStringLiteral("%1 non è UTF-8 valido.").arg(what);
        return false;
    }
    if (value)
        *value = decoded;
    return true;
}

bool normalizeRelativePath(const QString &input, QString *normalized, QString *error,
                           const QString &label)
{
    if (input.startsWith(QLatin1Char('/'))) {
        if (error)
            *error = QStringLiteral("%1 assoluto non consentito: %2").arg(label, input.left(160));
        return false;
    }

    QStringList stack;
    const QStringList components = input.split(QLatin1Char('/'), Qt::KeepEmptyParts);
    for (const QString &component : components) {
        if (component.isEmpty() || component == QStringLiteral("."))
            continue;
        if (component == QStringLiteral("..")) {
            if (stack.isEmpty()) {
                if (error)
                    *error = QStringLiteral("%1 esce dalla radice archivio: %2").arg(label, input.left(160));
                return false;
            }
            stack.removeLast();
            continue;
        }
        if (component.contains(QLatin1Char('\0'))) {
            if (error)
                *error = QStringLiteral("%1 contiene NUL.").arg(label);
            return false;
        }
        stack.append(component);
    }
    if (normalized)
        *normalized = stack.join(QLatin1Char('/'));
    return true;
}

QString parentPath(const QString &path)
{
    const qsizetype slash = path.lastIndexOf(QLatin1Char('/'));
    return slash < 0 ? QString() : path.left(slash);
}

bool validateSymlinkTarget(const QString &entryPath, const QString &target, QString *error)
{
    // Absolute symlinks are preserved as links but are never traversed by the
    // extraction/apply engine. Relative links must remain lexically inside the
    // archive root; this keeps the long-standing negative case
    // "link -> ../escape" while allowing "sub/link -> ../file".
    if (target.startsWith(QLatin1Char('/')))
        return true;

    const QString base = parentPath(entryPath);
    const QString combined = base.isEmpty() ? target : base + QLatin1Char('/') + target;
    QString ignored;
    return normalizeRelativePath(combined, &ignored, error,
                                 QStringLiteral("bersaglio del symlink"));
}

bool resolveHardTarget(const QString &path, ArchivePlan *plan, QSet<QString> *visiting,
                       QString *resolved, QString *error)
{
    const auto it = plan->index.constFind(path);
    if (it == plan->index.cend()) {
        if (error)
            *error = QStringLiteral("Hard link verso membro assente: %1").arg(path.left(160));
        return false;
    }
    const PlannedEntry &entry = plan->entries.at(it.value());
    if (entry.kind == EntryKind::Regular) {
        if (resolved)
            *resolved = entry.path;
        return true;
    }
    if (entry.kind != EntryKind::Hardlink) {
        if (error)
            *error = QStringLiteral("Hard link verso tipo non supportato: %1").arg(path.left(160));
        return false;
    }
    if (visiting->contains(path)) {
        if (error)
            *error = QStringLiteral("Ciclo di hard link nell'archivio: %1").arg(path.left(160));
        return false;
    }
    visiting->insert(path);
    const bool ok = resolveHardTarget(entry.linkTarget, plan, visiting, resolved, error);
    visiting->remove(path);
    return ok;
}

bool readPlan(const QString &archivePath, ArchivePlan *plan, QString *error)
{
    ArchiveApi api;
    if (!api.load(error))
        return false;
    ArchiveReader reader(&api, archivePath, error);
    if (!reader.valid())
        return false;

    int seenHeaders = 0;
    archive_entry *rawEntry = nullptr;
    for (;;) {
        const int status = reader.next(&rawEntry);
        if (status == kArchiveEof)
            break;
        if (status != kArchiveOk) {
            if (error)
                *error = reader.archiveError(QStringLiteral("Archivio troncato o non valido"));
            return false;
        }
        if (++seenHeaders > kMaxMembers) {
            if (error)
                *error = QStringLiteral("Archivio con troppi membri (limite %1).").arg(kMaxMembers);
            return false;
        }

        QString rawPath;
        if (!decodeArchiveText(api.pathname(rawEntry), &rawPath,
                               QStringLiteral("nome membro"), error))
            return false;
        QString path;
        if (!normalizeRelativePath(rawPath, &path, error, QStringLiteral("percorso membro")))
            return false;

        const mode_t fullMode = api.entryMode(rawEntry);
        const mode_t type = fullMode & S_IFMT;
        const char *hardRaw = api.hardlink(rawEntry);
        const char *symbolRaw = api.symlink(rawEntry);

        if (path.isEmpty()) {
            if (type == S_IFDIR) {
                if (reader.skip() < kArchiveOk) {
                    if (error)
                        *error = reader.archiveError(QStringLiteral("Errore leggendo la radice archivio"));
                    return false;
                }
                continue;
            }
            if (error)
                *error = QStringLiteral("Membro archivio con percorso vuoto.");
            return false;
        }
        if (plan->index.contains(path)) {
            if (error)
                *error = QStringLiteral("Membro duplicato o ambiguo nell'archivio: %1").arg(path.left(160));
            return false;
        }

        PlannedEntry entry;
        entry.path = path;
        entry.mode = fullMode & 0777;
        entry.mtime = api.entryMtime(rawEntry);

        if (hardRaw) {
            QString targetRaw;
            if (!decodeArchiveText(hardRaw, &targetRaw, QStringLiteral("target hard link"), error))
                return false;
            QString target;
            if (!normalizeRelativePath(targetRaw, &target, error,
                                       QStringLiteral("target hard link")) || target.isEmpty())
                return false;
            entry.kind = EntryKind::Hardlink;
            entry.linkTarget = target;
        } else if (type == S_IFREG) {
            const qint64 signedSize = api.entrySize(rawEntry);
            if (signedSize < 0 || quint64(signedSize) > kMaxSingleFileBytes) {
                if (error)
                    *error = QStringLiteral("Dimensione non supportata per %1.").arg(path.left(160));
                return false;
            }
            entry.kind = EntryKind::Regular;
            entry.size = quint64(signedSize);
            if (plan->totalBytes > kMaxTotalFileBytes - entry.size) {
                if (error)
                    *error = QStringLiteral("Dimensione totale dell'archivio oltre il limite di sicurezza.");
                return false;
            }
            plan->totalBytes += entry.size;
        } else if (type == S_IFDIR) {
            entry.kind = EntryKind::Directory;
        } else if (type == S_IFLNK || symbolRaw) {
            QString target;
            if (!decodeArchiveText(symbolRaw, &target, QStringLiteral("target symlink"), error))
                return false;
            if (!validateSymlinkTarget(path, target, error))
                return false;
            entry.kind = EntryKind::Symlink;
            entry.linkTarget = target;
        } else {
            if (error)
                *error = QStringLiteral("Tipo di membro archivio non supportato: %1").arg(path.left(160));
            return false;
        }

        plan->index.insert(path, plan->entries.size());
        plan->entries.append(entry);
        if (reader.skip() < kArchiveOk) {
            if (error)
                *error = reader.archiveError(QStringLiteral("Errore leggendo i dati dell'archivio"));
            return false;
        }
    }

    // A member may only have directory members (or implicit directories) as
    // path prefixes. This rejects file/link vs child conflicts independently of
    // archive order.
    for (const PlannedEntry &entry : std::as_const(plan->entries)) {
        QString prefix;
        const QStringList parts = entry.path.split(QLatin1Char('/'));
        for (int i = 0; i + 1 < parts.size(); ++i) {
            if (!prefix.isEmpty())
                prefix += QLatin1Char('/');
            prefix += parts.at(i);
            const auto parentIt = plan->index.constFind(prefix);
            if (parentIt != plan->index.cend()
                && plan->entries.at(parentIt.value()).kind != EntryKind::Directory) {
                if (error)
                    *error = QStringLiteral("Conflitto file/directory nell'archivio: %1").arg(prefix.left(160));
                return false;
            }
        }
    }

    for (PlannedEntry &entry : plan->entries) {
        if (entry.kind != EntryKind::Hardlink)
            continue;
        QSet<QString> visiting;
        visiting.insert(entry.path);
        QString resolved;
        if (!resolveHardTarget(entry.linkTarget, plan, &visiting, &resolved, error))
            return false;
        entry.resolvedHardTarget = resolved;
    }

    return true;
}

ScopedFd openRootNoLinks(const QString &path, QString *error)
{
    const QString clean = QDir::cleanPath(path);
    if (!QDir::isAbsolutePath(clean)) {
        if (error)
            *error = QStringLiteral("Radice destinazione non assoluta.");
        return {};
    }
    ScopedFd current(::open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC));
    if (!current.valid()) {
        if (error)
            *error = errnoText(QStringLiteral("Impossibile aprire /"));
        return {};
    }
    for (const QString &part : clean.split(QLatin1Char('/'), Qt::SkipEmptyParts)) {
        const QByteArray name = part.toUtf8();
        const int next = ::openat(current.get(), name.constData(),
                                  O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        if (next < 0) {
            if (error)
                *error = errnoText(QStringLiteral("Radice non attraversabile senza symlink"));
            return {};
        }
        current = ScopedFd(next);
    }
    return current;
}

int openDirectoryComponent(int parentFd, const QByteArray &name, bool noXdev)
{
#ifdef SYS_openat2
    struct open_how how {};
    how.flags = O_RDONLY | O_DIRECTORY | O_CLOEXEC;
    how.resolve = RESOLVE_BENEATH | RESOLVE_NO_SYMLINKS | RESOLVE_NO_MAGICLINKS;
    if (noXdev)
        how.resolve |= RESOLVE_NO_XDEV;
    return int(::syscall(SYS_openat2, parentFd, name.constData(), &how, sizeof(how)));
#else
    Q_UNUSED(parentFd)
    Q_UNUSED(name)
    Q_UNUSED(noXdev)
    errno = ENOSYS;
    return -1;
#endif
}

bool ensureDirectoryComponent(int parentFd, const QString &name, bool create,
                              bool noXdev, ScopedFd *opened, bool *mutated,
                              QString *error)
{
    const QByteArray encoded = name.toUtf8();
    int fd = openDirectoryComponent(parentFd, encoded, noXdev);
    if (fd < 0 && errno == ENOENT && create) {
        if (::mkdirat(parentFd, encoded.constData(), 0700) != 0) {
            if (error)
                *error = errnoText(QStringLiteral("Impossibile creare directory di ripristino"));
            return false;
        }
        if (mutated)
            *mutated = true;
        fd = openDirectoryComponent(parentFd, encoded, noXdev);
    }
    if (fd < 0) {
        if (error) {
            if (errno == ENOSYS)
                *error = QStringLiteral("openat2 non disponibile: ripristino confinato rifiutato.");
            else if (errno == EXDEV)
                *error = QStringLiteral("Ripristino bloccato: attraversamento di mount/bind mount non consentito.");
            else
                *error = errnoText(QStringLiteral("Componente destinazione non attraversabile in sicurezza"));
        }
        return false;
    }
    if (opened)
        *opened = ScopedFd(fd);
    else
        ::close(fd);
    return true;
}

bool openParent(int rootFd, const QString &path, bool createParents, bool noXdev,
                ScopedFd *parent, QByteArray *leaf, bool *mutated, QString *error)
{
    QStringList parts = path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (parts.isEmpty()) {
        if (error)
            *error = QStringLiteral("Percorso membro vuoto.");
        return false;
    }
    const QString leafPart = parts.takeLast();
    ScopedFd current(::dup(rootFd));
    if (!current.valid()) {
        if (error)
            *error = errnoText(QStringLiteral("Impossibile duplicare il descriptor radice"));
        return false;
    }
    for (const QString &part : parts) {
        ScopedFd next;
        if (!ensureDirectoryComponent(current.get(), part, createParents, noXdev,
                                      &next, mutated, error))
            return false;
        current = std::move(next);
    }
    if (parent)
        *parent = std::move(current);
    if (leaf)
        *leaf = leafPart.toUtf8();
    return true;
}

bool ensureDirectoryPath(int rootFd, const QString &path, bool create, bool noXdev,
                         ScopedFd *directory, bool *mutated, QString *error)
{
    ScopedFd current(::dup(rootFd));
    if (!current.valid()) {
        if (error)
            *error = errnoText(QStringLiteral("Impossibile duplicare il descriptor radice"));
        return false;
    }
    for (const QString &part : path.split(QLatin1Char('/'), Qt::SkipEmptyParts)) {
        ScopedFd next;
        if (!ensureDirectoryComponent(current.get(), part, create, noXdev,
                                      &next, mutated, error))
            return false;
        current = std::move(next);
    }
    if (directory)
        *directory = std::move(current);
    return true;
}

bool pwriteAll(int fd, const char *data, size_t size, qint64 offset)
{
    size_t done = 0;
    while (done < size) {
        const ssize_t written = ::pwrite(fd, data + done, size - done, off_t(offset + qint64(done)));
        if (written < 0) {
            if (errno == EINTR)
                continue;
            return false;
        }
        if (written == 0) {
            errno = EIO;
            return false;
        }
        done += size_t(written);
    }
    return true;
}

bool copyFd(int source, int destination)
{
    QByteArray buffer(256 * 1024, Qt::Uninitialized);
    for (;;) {
        const ssize_t count = ::read(source, buffer.data(), size_t(buffer.size()));
        if (count < 0) {
            if (errno == EINTR)
                continue;
            return false;
        }
        if (count == 0)
            return true;
        size_t done = 0;
        while (done < size_t(count)) {
            const ssize_t written = ::write(destination, buffer.constData() + done,
                                            size_t(count) - done);
            if (written < 0) {
                if (errno == EINTR)
                    continue;
                return false;
            }
            if (written == 0) {
                errno = EIO;
                return false;
            }
            done += size_t(written);
        }
    }
}

void applyTimes(int fd, qint64 seconds)
{
    if (seconds <= 0)
        return;
    struct timespec times[2] {};
    times[0].tv_nsec = UTIME_OMIT;
    times[1].tv_sec = time_t(seconds);
    times[1].tv_nsec = 0;
    (void)::futimens(fd, times);
}

bool extractRegularFiles(const QString &archivePath, const ArchivePlan &plan,
                         int stagingRootFd, QString *error)
{
    ArchiveApi api;
    if (!api.load(error))
        return false;
    ArchiveReader reader(&api, archivePath, error);
    if (!reader.valid())
        return false;

    archive_entry *rawEntry = nullptr;
    for (;;) {
        const int status = reader.next(&rawEntry);
        if (status == kArchiveEof)
            break;
        if (status != kArchiveOk) {
            if (error)
                *error = reader.archiveError(QStringLiteral("Archivio troncato durante l'estrazione in staging"));
            return false;
        }

        QString rawPath;
        if (!decodeArchiveText(api.pathname(rawEntry), &rawPath,
                               QStringLiteral("nome membro"), error))
            return false;
        QString path;
        if (!normalizeRelativePath(rawPath, &path, error, QStringLiteral("percorso membro")))
            return false;
        if (path.isEmpty()) {
            if (reader.skip() < kArchiveOk)
                return false;
            continue;
        }

        const auto plannedIt = plan.index.constFind(path);
        if (plannedIt == plan.index.cend()) {
            if (error)
                *error = QStringLiteral("Il secondo passaggio dell'archivio non coincide con il piano validato.");
            return false;
        }
        const PlannedEntry &entry = plan.entries.at(plannedIt.value());
        if (entry.kind != EntryKind::Regular) {
            if (reader.skip() < kArchiveOk) {
                if (error)
                    *error = reader.archiveError(QStringLiteral("Errore saltando membro non regolare"));
                return false;
            }
            continue;
        }

        ScopedFd parent;
        QByteArray leaf;
        bool ignoredMutation = false;
        if (!openParent(stagingRootFd, entry.path, true, false,
                        &parent, &leaf, &ignoredMutation, error))
            return false;
        ScopedFd output(::openat(parent.get(), leaf.constData(),
                                 O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW,
                                 0600));
        if (!output.valid()) {
            if (error)
                *error = errnoText(QStringLiteral("Impossibile creare file nello staging"));
            return false;
        }

        quint64 highestEnd = 0;
        for (;;) {
            const void *buffer = nullptr;
            size_t size = 0;
            qint64 offset = 0;
            const int dataStatus = reader.dataBlock(&buffer, &size, &offset);
            if (dataStatus == kArchiveEof)
                break;
            if (dataStatus != kArchiveOk || offset < 0
                || quint64(offset) > entry.size
                || quint64(size) > entry.size - quint64(offset)) {
                if (error)
                    *error = reader.archiveError(QStringLiteral("Dati file incoerenti nell'archivio"));
                return false;
            }
            if (size > 0 && !pwriteAll(output.get(), static_cast<const char *>(buffer), size, offset)) {
                if (error)
                    *error = errnoText(QStringLiteral("Errore scrivendo file nello staging"));
                return false;
            }
            highestEnd = std::max(highestEnd, quint64(offset) + quint64(size));
        }
        if (highestEnd > entry.size || ::ftruncate(output.get(), off_t(entry.size)) != 0) {
            if (error)
                *error = errnoText(QStringLiteral("Dimensione file staging non applicabile"));
            return false;
        }
        if (::fchmod(output.get(), entry.mode & 0777) != 0) {
            if (error)
                *error = errnoText(QStringLiteral("Permessi file staging non applicabili"));
            return false;
        }
        applyTimes(output.get(), entry.mtime);
    }
    return true;
}

bool buildStagingLinks(const ArchivePlan &plan, int stagingRootFd, QString *error)
{
    bool ignoredMutation = false;
    for (const PlannedEntry &entry : plan.entries) {
        if (entry.kind == EntryKind::Directory) {
            ScopedFd dir;
            if (!ensureDirectoryPath(stagingRootFd, entry.path, true, false,
                                     &dir, &ignoredMutation, error))
                return false;
        }
    }

    for (const PlannedEntry &entry : plan.entries) {
        if (entry.kind != EntryKind::Hardlink)
            continue;
        ScopedFd sourceParent;
        QByteArray sourceLeaf;
        if (!openParent(stagingRootFd, entry.resolvedHardTarget, false, false,
                        &sourceParent, &sourceLeaf, &ignoredMutation, error))
            return false;
        ScopedFd targetParent;
        QByteArray targetLeaf;
        if (!openParent(stagingRootFd, entry.path, true, false,
                        &targetParent, &targetLeaf, &ignoredMutation, error))
            return false;
        if (::linkat(sourceParent.get(), sourceLeaf.constData(),
                     targetParent.get(), targetLeaf.constData(), 0) != 0) {
            if (error)
                *error = errnoText(QStringLiteral("Impossibile creare hard link nello staging"));
            return false;
        }
    }

    for (const PlannedEntry &entry : plan.entries) {
        if (entry.kind != EntryKind::Symlink)
            continue;
        ScopedFd parent;
        QByteArray leaf;
        if (!openParent(stagingRootFd, entry.path, true, false,
                        &parent, &leaf, &ignoredMutation, error))
            return false;
        const QByteArray target = entry.linkTarget.toUtf8();
        if (::symlinkat(target.constData(), parent.get(), leaf.constData()) != 0) {
            if (error)
                *error = errnoText(QStringLiteral("Impossibile creare symlink nello staging"));
            return false;
        }
    }

    QVector<const PlannedEntry *> directories;
    for (const PlannedEntry &entry : plan.entries) {
        if (entry.kind == EntryKind::Directory)
            directories.append(&entry);
    }
    std::sort(directories.begin(), directories.end(), [](const PlannedEntry *a, const PlannedEntry *b) {
        return a->path.count(QLatin1Char('/')) > b->path.count(QLatin1Char('/'));
    });
    for (const PlannedEntry *entry : directories) {
        ScopedFd dir;
        if (!ensureDirectoryPath(stagingRootFd, entry->path, false, false,
                                 &dir, &ignoredMutation, error))
            return false;
        (void)::fchmod(dir.get(), entry->mode & 0777);
        applyTimes(dir.get(), entry->mtime);
    }
    return true;
}

QByteArray temporaryLeaf()
{
    return QStringLiteral(".kriscc-restore-%1")
        .arg(QRandomGenerator::global()->generate64(), 0, 16).toUtf8();
}

bool publishRegular(const PlannedEntry &entry, int stagingRootFd, int destinationRootFd,
                    bool *mutated, QString *error)
{
    ScopedFd sourceParent;
    QByteArray sourceLeaf;
    bool stagingMutated = false;
    if (!openParent(stagingRootFd, entry.path, false, false,
                    &sourceParent, &sourceLeaf, &stagingMutated, error))
        return false;
    ScopedFd source(::openat(sourceParent.get(), sourceLeaf.constData(),
                             O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
    if (!source.valid()) {
        if (error)
            *error = errnoText(QStringLiteral("File staging non apribile"));
        return false;
    }

    ScopedFd destinationParent;
    QByteArray destinationLeaf;
    if (!openParent(destinationRootFd, entry.path, true, true,
                    &destinationParent, &destinationLeaf, mutated, error))
        return false;

    QByteArray temp;
    ScopedFd output;
    for (int attempt = 0; attempt < 8 && !output.valid(); ++attempt) {
        temp = temporaryLeaf();
        output = ScopedFd(::openat(destinationParent.get(), temp.constData(),
                                   O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW,
                                   0600));
        if (!output.valid() && errno != EEXIST)
            break;
    }
    if (!output.valid()) {
        if (error)
            *error = errnoText(QStringLiteral("Impossibile creare file temporaneo nella destinazione"));
        return false;
    }

    if (!copyFd(source.get(), output.get())
        || ::fchmod(output.get(), entry.mode & 0777) != 0
        || ::fsync(output.get()) != 0) {
        const int saved = errno;
        (void)::unlinkat(destinationParent.get(), temp.constData(), 0);
        errno = saved;
        if (error)
            *error = errnoText(QStringLiteral("Impossibile preparare il file di ripristino"));
        return false;
    }
    applyTimes(output.get(), entry.mtime);

    if (::renameat(destinationParent.get(), temp.constData(),
                   destinationParent.get(), destinationLeaf.constData()) != 0) {
        const int saved = errno;
        (void)::unlinkat(destinationParent.get(), temp.constData(), 0);
        errno = saved;
        if (error)
            *error = errnoText(QStringLiteral("Conflitto pubblicando il file ripristinato"));
        return false;
    }
    if (mutated)
        *mutated = true;
    return true;
}

bool publishHardlink(const PlannedEntry &entry, int destinationRootFd,
                     bool *mutated, QString *error)
{
    ScopedFd sourceParent;
    QByteArray sourceLeaf;
    if (!openParent(destinationRootFd, entry.resolvedHardTarget, false, true,
                    &sourceParent, &sourceLeaf, mutated, error))
        return false;
    struct stat sourceStat {};
    if (::fstatat(sourceParent.get(), sourceLeaf.constData(), &sourceStat,
                  AT_SYMLINK_NOFOLLOW) != 0 || !S_ISREG(sourceStat.st_mode)) {
        if (error)
            *error = QStringLiteral("Target hard link ripristinato non disponibile come file regolare.");
        return false;
    }

    ScopedFd targetParent;
    QByteArray targetLeaf;
    if (!openParent(destinationRootFd, entry.path, true, true,
                    &targetParent, &targetLeaf, mutated, error))
        return false;

    QByteArray temp;
    bool linked = false;
    for (int attempt = 0; attempt < 8 && !linked; ++attempt) {
        temp = temporaryLeaf();
        if (::linkat(sourceParent.get(), sourceLeaf.constData(),
                     targetParent.get(), temp.constData(), 0) == 0) {
            linked = true;
            break;
        }
        if (errno != EEXIST)
            break;
    }
    if (!linked) {
        if (error)
            *error = errnoText(QStringLiteral("Impossibile preparare hard link di ripristino"));
        return false;
    }

    struct stat linkedStat {};
    if (::fstatat(targetParent.get(), temp.constData(), &linkedStat,
                  AT_SYMLINK_NOFOLLOW) != 0
        || linkedStat.st_dev != sourceStat.st_dev || linkedStat.st_ino != sourceStat.st_ino) {
        (void)::unlinkat(targetParent.get(), temp.constData(), 0);
        if (error)
            *error = QStringLiteral("Identità hard link cambiata durante il ripristino.");
        return false;
    }

    if (::renameat(targetParent.get(), temp.constData(),
                   targetParent.get(), targetLeaf.constData()) != 0) {
        const int saved = errno;
        (void)::unlinkat(targetParent.get(), temp.constData(), 0);
        errno = saved;
        if (error)
            *error = errnoText(QStringLiteral("Conflitto pubblicando hard link ripristinato"));
        return false;
    }
    if (mutated)
        *mutated = true;
    return true;
}

bool publishSymlink(const PlannedEntry &entry, int destinationRootFd,
                    bool *mutated, QString *error)
{
    ScopedFd parent;
    QByteArray leaf;
    if (!openParent(destinationRootFd, entry.path, true, true,
                    &parent, &leaf, mutated, error))
        return false;

    QByteArray temp;
    bool linked = false;
    const QByteArray target = entry.linkTarget.toUtf8();
    for (int attempt = 0; attempt < 8 && !linked; ++attempt) {
        temp = temporaryLeaf();
        if (::symlinkat(target.constData(), parent.get(), temp.constData()) == 0) {
            linked = true;
            break;
        }
        if (errno != EEXIST)
            break;
    }
    if (!linked) {
        if (error)
            *error = errnoText(QStringLiteral("Impossibile preparare symlink di ripristino"));
        return false;
    }
    if (::renameat(parent.get(), temp.constData(), parent.get(), leaf.constData()) != 0) {
        const int saved = errno;
        (void)::unlinkat(parent.get(), temp.constData(), 0);
        errno = saved;
        if (error)
            *error = errnoText(QStringLiteral("Conflitto pubblicando symlink ripristinato"));
        return false;
    }
    if (mutated)
        *mutated = true;
    return true;
}

ArchiveOperationResult applyPlan(const ArchivePlan &plan, int stagingRootFd,
                                 const QString &destinationRoot)
{
    ArchiveOperationResult result;
    result.members = plan.entries.size();
    QString error;
    ScopedFd destination = openRootNoLinks(destinationRoot, &error);
    if (!destination.valid()) {
        result.message = error;
        return result;
    }

    bool mutated = false;
    for (const PlannedEntry &entry : plan.entries) {
        if (entry.kind != EntryKind::Directory)
            continue;
        ScopedFd directory;
        if (!ensureDirectoryPath(destination.get(), entry.path, true, true,
                                 &directory, &mutated, &error)) {
            result.partial = mutated;
            result.message = error;
            return result;
        }
        ++result.applied;
    }

    for (const PlannedEntry &entry : plan.entries) {
        if (entry.kind != EntryKind::Regular)
            continue;
        if (!publishRegular(entry, stagingRootFd, destination.get(), &mutated, &error)) {
            result.partial = mutated;
            result.message = QStringLiteral("Ripristino interrotto su %1: %2")
                                 .arg(entry.path.left(160), error);
            return result;
        }
        ++result.applied;
    }

    for (const PlannedEntry &entry : plan.entries) {
        if (entry.kind != EntryKind::Hardlink)
            continue;
        if (!publishHardlink(entry, destination.get(), &mutated, &error)) {
            result.partial = mutated;
            result.message = QStringLiteral("Ripristino interrotto su %1: %2")
                                 .arg(entry.path.left(160), error);
            return result;
        }
        ++result.applied;
    }

    for (const PlannedEntry &entry : plan.entries) {
        if (entry.kind != EntryKind::Symlink)
            continue;
        if (!publishSymlink(entry, destination.get(), &mutated, &error)) {
            result.partial = mutated;
            result.message = QStringLiteral("Ripristino interrotto su %1: %2")
                                 .arg(entry.path.left(160), error);
            return result;
        }
        ++result.applied;
    }

    QVector<const PlannedEntry *> directories;
    for (const PlannedEntry &entry : plan.entries) {
        if (entry.kind == EntryKind::Directory)
            directories.append(&entry);
    }
    std::sort(directories.begin(), directories.end(), [](const PlannedEntry *a, const PlannedEntry *b) {
        return a->path.count(QLatin1Char('/')) > b->path.count(QLatin1Char('/'));
    });
    for (const PlannedEntry *entry : directories) {
        ScopedFd directory;
        if (!ensureDirectoryPath(destination.get(), entry->path, false, true,
                                 &directory, &mutated, &error)) {
            result.partial = mutated;
            result.message = error;
            return result;
        }
        if (::fchmod(directory.get(), entry->mode & 0777) != 0) {
            result.partial = mutated;
            result.message = errnoText(QStringLiteral("Impossibile applicare permessi directory"));
            return result;
        }
        applyTimes(directory.get(), entry->mtime);
    }

    result.success = true;
    result.partial = false;
    result.message = QStringLiteral("Ripristino confinato completato: %1 membri applicati.").arg(result.applied);
    return result;
}
}

ArchiveOperationResult ArchiveRestoreEngine::validate(const QString &archivePath)
{
    ArchiveOperationResult result;
    ArchivePlan plan;
    QString error;
    if (!readPlan(archivePath, &plan, &error)) {
        result.message = error.isEmpty() ? QStringLiteral("Archivio non valido.") : error;
        return result;
    }
    result.success = true;
    result.members = plan.entries.size();
    result.message = QStringLiteral("Archivio valido: %1 membri supportati.").arg(result.members);
    return result;
}

ArchiveOperationResult ArchiveRestoreEngine::restore(const QString &archivePath,
                                                      const QString &destinationRoot)
{
    ArchiveOperationResult result;
    ArchivePlan plan;
    QString error;
    if (!readPlan(archivePath, &plan, &error)) {
        result.message = error.isEmpty() ? QStringLiteral("Archivio non valido.") : error;
        return result;
    }
    result.members = plan.entries.size();

    QTemporaryDir staging(QDir::tempPath() + QStringLiteral("/kriscc-restore-XXXXXX"));
    if (!staging.isValid()) {
        result.message = QStringLiteral("Impossibile creare lo staging privato del ripristino.");
        return result;
    }
    QFile::setPermissions(staging.path(), QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                          | QFileDevice::ExeOwner);

    ScopedFd stagingRoot = openRootNoLinks(staging.path(), &error);
    if (!stagingRoot.valid()) {
        result.message = error;
        return result;
    }

    if (!extractRegularFiles(archivePath, plan, stagingRoot.get(), &error)
        || !buildStagingLinks(plan, stagingRoot.get(), &error)) {
        result.message = error.isEmpty() ? QStringLiteral("Estrazione nello staging non riuscita.") : error;
        return result;
    }

    return applyPlan(plan, stagingRoot.get(), destinationRoot);
}
