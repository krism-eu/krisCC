#include "PackageSearch.h"

#include "ContractParsers.h"
#include "ProcessRunner.h"

#include <QFile>
#include <QFileInfo>
#include <QTextStream>

namespace {
constexpr qsizetype kInstalledOutputLimit = 8 * 1024 * 1024;
constexpr qsizetype kSearchOutputLimit = 2 * 1024 * 1024;
constexpr qsizetype kListOutputLimit = 8 * 1024 * 1024;
}

PackageSearch::PackageSearch(QObject *parent) : QAbstractListModel(parent)
{
    QFile file(QStringLiteral("/usr/share/krisos/owned-packages.txt"));
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&file);
        while (!in.atEnd()) {
            const QString name = in.readLine().trimmed();
            if (!name.isEmpty() && !name.startsWith(QLatin1Char('#'))) m_owned.insert(name);
        }
    }
    refreshPersistentSet();
}

int PackageSearch::rowCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : m_results.size(); }

QVariant PackageSearch::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_results.size()) return {};
    const Entry &entry = m_results.at(index.row());
    switch (role) {
    case NameRole: return entry.name;
    case SummaryRole: return entry.summary;
    case InstalledRole: return entry.installed;
    case OwnedRole: return entry.owned;
    case PersistentRole: return entry.persistent;
    case VersionRole: return entry.version;
    case RepositoryRole: return entry.repository;
    case ArchRole: return entry.arch;
    case DownloadSizeRole: return QVariant::fromValue<qulonglong>(entry.downloadSize);
    case InstallSizeRole: return QVariant::fromValue<qulonglong>(entry.installSize);
    default: return {};
    }
}

QHash<int, QByteArray> PackageSearch::roleNames() const
{
    return {{NameRole,"name"},{SummaryRole,"summary"},{InstalledRole,"installed"},
            {OwnedRole,"owned"},{PersistentRole,"persistent"},{VersionRole,"version"},
            {RepositoryRole,"repository"},{ArchRole,"arch"},
            {DownloadSizeRole,"downloadSize"},{InstallSizeRole,"installSize"}};
}

void PackageSearch::search(const QString &term)
{
    const QString sanitized = sanitizeTerm(term);
    refreshPersistentSet();
    ++m_generation;
    stopActiveProcess();
    if (sanitized.size() < 2) {
        clearResults(); setSearching(false); emit searchFinished(); return;
    }
    if (m_truncated) { m_truncated=false; emit truncatedChanged(); }
    setSearching(true);
    startInstalledQuery(sanitized);
}

void PackageSearch::loadInstalled(const QString &filter)
{
    static const QSet<QString> allowed={QStringLiteral("all"),QStringLiteral("base"),QStringLiteral("persistent"),QStringLiteral("local")};
    m_installedFilter=allowed.contains(filter)?filter:QStringLiteral("all");
    refreshPersistentSet(); ++m_generation; stopActiveProcess(); startListQuery(QStringLiteral("--installed"),true);
}

void PackageSearch::loadUpgrades(){refreshPersistentSet();++m_generation;stopActiveProcess();startListQuery(QStringLiteral("--upgrades"),true);}
void PackageSearch::loadRecent(){refreshPersistentSet();++m_generation;stopActiveProcess();startListQuery(QStringLiteral("--recent"),false);}

void PackageSearch::startInstalledQuery(const QString &term)
{
    const quint64 generation=m_generation;
    auto *runner=new ProcessRunner(this); m_runner=runner;
    connect(runner,&ProcessRunner::finished,this,[this,runner,term,generation](ProcessRunner::Outcome outcome,int,const QByteArray &out,const QByteArray &,const QString &){
        if(runner!=m_runner||generation!=m_generation){runner->deleteLater();return;}
        m_runner=nullptr; runner->deleteLater(); m_installed.clear();
        if(outcome==ProcessRunner::Success){
            for(const QByteArray &line:out.split('\n')){const QString name=QString::fromUtf8(line).trimmed();if(!name.isEmpty())m_installed.insert(name);}
        } else if(outcome==ProcessRunner::TimedOut) {
            emit searchError(tr("Tempo massimo superato durante la lettura dei pacchetti installati; la ricerca continua senza cache locale aggiornata."));
        } else if(outcome==ProcessRunner::FailedToStart) {
            emit searchError(tr("Impossibile avviare rpm per leggere i pacchetti installati."));
        }
        startRepoQuery(term);
    });
    ProcessRunner::Options o; o.program=QStringLiteral("/usr/bin/rpm");
    o.arguments={QStringLiteral("-qa"),QStringLiteral("--qf"),QStringLiteral("%{NAME}\n")};
    o.timeoutMs=30*1000;o.maxOutputBytes=kInstalledOutputLimit;o.mergedChannels=false;
    if(!runner->start(o)){m_runner=nullptr;runner->deleteLater();m_installed.clear();emit searchError(tr("Impossibile inizializzare rpm."));startRepoQuery(term);}
}

void PackageSearch::startRepoQuery(const QString &term)
{
    const quint64 generation=m_generation;
    auto *runner=new ProcessRunner(this); m_runner=runner;
    connect(runner,&ProcessRunner::finished,this,[this,runner,generation](ProcessRunner::Outcome outcome,int,const QByteArray &out,const QByteArray &err,const QString &error){
        if(runner!=m_runner||generation!=m_generation){runner->deleteLater();return;}
        m_runner=nullptr;runner->deleteLater();
        if(outcome!=ProcessRunner::Success){
            clearResults();setSearching(false);
            if(outcome==ProcessRunner::TimedOut) emit searchError(tr("Tempo massimo superato durante la ricerca DNF5."));
            else if(outcome==ProcessRunner::FailedToStart) emit searchError(tr("Impossibile avviare dnf5: %1").arg(error));
            else {const QString d=QString::fromUtf8(err.isEmpty()?out:err).trimmed();emit searchError(d.isEmpty()?tr("La ricerca dnf5 non e' riuscita."):d);}
            emit searchFinished();return;
        }
        const auto parsed=ContractParsers::parseDnfRepoquery(out);
        if(!parsed.ok()){clearResults();setSearching(false);emit searchError(tr("Formato di output DNF5 repoquery non riconosciuto."));emit searchFinished();return;}
        QList<Entry> entries; bool truncated=false;
        for(const QVariant &value:parsed.values){
            if(entries.size()>=100){truncated=true;break;}
            const QVariantMap row=value.toMap(); Entry e;
            e.name=row.value(QStringLiteral("name")).toString();e.summary=row.value(QStringLiteral("summary")).toString();
            e.version=row.value(QStringLiteral("version")).toString();e.repository=row.value(QStringLiteral("repository")).toString();
            e.arch=row.value(QStringLiteral("arch")).toString();e.downloadSize=row.value(QStringLiteral("downloadSize")).toULongLong();
            e.installSize=row.value(QStringLiteral("installSize")).toULongLong();e.installed=m_installed.contains(e.name);
            e.owned=m_owned.contains(e.name);e.persistent=m_persistent.contains(e.name);entries.append(e);
        }
        beginResetModel();m_results=entries;endResetModel();
        if(m_truncated!=truncated){m_truncated=truncated;emit truncatedChanged();}
        emit countChanged();setSearching(false);emit searchFinished();
    });
    ProcessRunner::Options o;o.program=QStringLiteral("/usr/bin/dnf5");
    o.arguments={QStringLiteral("repoquery"),QStringLiteral("--available"),QStringLiteral("--latest-limit=1"),
                 QStringLiteral("--queryformat"),QStringLiteral("%{name}\t%{summary}\t%{evr}\t%{repoid}\t%{arch}\t%{downloadsize}\t%{installsize}\n"),
                 QStringLiteral("*")+term+QStringLiteral("*")};
    o.timeoutMs=2*60*1000;o.maxOutputBytes=kSearchOutputLimit;o.mergedChannels=false;
    if(!runner->start(o)){m_runner=nullptr;runner->deleteLater();clearResults();setSearching(false);emit searchError(tr("Impossibile inizializzare dnf5."));emit searchFinished();}
}

void PackageSearch::startListQuery(const QString &filter,bool installedEntries)
{
    const quint64 generation=m_generation;setSearching(true);clearResults();
    auto *runner=new ProcessRunner(this);m_runner=runner;
    connect(runner,&ProcessRunner::finished,this,[this,runner,generation,installedEntries](ProcessRunner::Outcome outcome,int,const QByteArray &out,const QByteArray &err,const QString &error){
        if(runner!=m_runner||generation!=m_generation){runner->deleteLater();return;}
        m_runner=nullptr;runner->deleteLater();
        if(outcome!=ProcessRunner::Success){
            setSearching(false);
            if(outcome==ProcessRunner::TimedOut)emit searchError(tr("Tempo massimo superato durante la lettura dell'elenco DNF5."));
            else if(outcome==ProcessRunner::FailedToStart)emit searchError(tr("Impossibile avviare dnf5: %1").arg(error));
            else {const QString d=QString::fromUtf8(err.isEmpty()?out:err).trimmed();emit searchError(d.isEmpty()?tr("Impossibile leggere l'elenco pacchetti DNF5."):d);}
            emit searchFinished();return;
        }
        const auto parsed=ContractParsers::parseDnfListJson(out);
        if(!parsed.ok()){setSearching(false);emit searchError(tr("Formato JSON DNF5 non riconosciuto."));emit searchFinished();return;}
        QList<Entry> entries;
        for(const QVariant &value:parsed.values){
            const QVariantMap row=value.toMap();Entry e;
            e.name=row.value(QStringLiteral("name")).toString();e.arch=row.value(QStringLiteral("arch")).toString();
            e.version=row.value(QStringLiteral("version")).toString();e.repository=row.value(QStringLiteral("repository")).toString();
            e.installed=installedEntries||m_installed.contains(e.name);e.owned=m_owned.contains(e.name);e.persistent=m_persistent.contains(e.name);
            if(installedEntries){
                const bool local=e.installed&&!e.owned&&!e.persistent;
                if(m_installedFilter==QStringLiteral("base")&&!e.owned)continue;
                if(m_installedFilter==QStringLiteral("persistent")&&!e.persistent)continue;
                if(m_installedFilter==QStringLiteral("local")&&!local)continue;
            }
            entries.append(e);
        }
        if(installedEntries){m_sourceResults=entries;applyLocalFilter();}
        else{beginResetModel();m_results=entries;endResetModel();emit countChanged();}
        setSearching(false);emit searchFinished();
    });
    ProcessRunner::Options o;o.program=QStringLiteral("/usr/bin/dnf5");
    o.arguments={QStringLiteral("list"),filter,QStringLiteral("--json")};
    o.timeoutMs=2*60*1000;o.maxOutputBytes=kListOutputLimit;o.mergedChannels=false;
    if(!runner->start(o)){m_runner=nullptr;runner->deleteLater();setSearching(false);emit searchError(tr("Impossibile inizializzare dnf5."));emit searchFinished();}
}

void PackageSearch::clearResults(){if(m_results.isEmpty())return;beginResetModel();m_results.clear();m_sourceResults.clear();endResetModel();emit countChanged();}
void PackageSearch::setSearching(bool searching){if(m_searching==searching)return;m_searching=searching;emit searchingChanged();}

void PackageSearch::stopActiveProcess()
{
    if(!m_runner)return;
    ProcessRunner *runner=m_runner;
    m_runner=nullptr;
    if(runner->running()){
        connect(runner,&ProcessRunner::finished,runner,&QObject::deleteLater,Qt::UniqueConnection);
        runner->cancel();
    } else runner->deleteLater();
}

void PackageSearch::refreshPersistentSet()
{
    m_persistent.clear();QFile file(QStringLiteral("/var/lib/krisos/packages.list"));
    if(!file.open(QIODevice::ReadOnly|QIODevice::Text))return;
    while(!file.atEnd()){const QString line=QString::fromUtf8(file.readLine()).trimmed();if(!line.isEmpty()&&!line.startsWith(QLatin1Char('#')))m_persistent.insert(line);}
}

void PackageSearch::setLocalFilter(const QString &text){const QString next=text.trimmed();if(m_localFilter==next)return;m_localFilter=next;applyLocalFilter();}
void PackageSearch::applyLocalFilter()
{
    QList<Entry> filtered;
    if(m_localFilter.isEmpty())filtered=m_sourceResults;
    else for(const Entry &e:m_sourceResults){
        if(e.name.contains(m_localFilter,Qt::CaseInsensitive)||e.version.contains(m_localFilter,Qt::CaseInsensitive)
           ||e.repository.contains(m_localFilter,Qt::CaseInsensitive)||e.arch.contains(m_localFilter,Qt::CaseInsensitive))filtered.append(e);
    }
    beginResetModel();m_results=filtered;endResetModel();emit countChanged();
}

QString PackageSearch::sanitizeTerm(const QString &term)
{
    QString out;out.reserve(term.size());bool pendingSeparator=false;
    for(const QChar ch:term.trimmed()){
        if(ch.isLetterOrNumber()||QStringLiteral("._+:-").contains(ch)){if(pendingSeparator&&!out.isEmpty())out+=QLatin1Char('*');out+=ch;pendingSeparator=false;}
        else if(ch.isSpace())pendingSeparator=true;
    }
    return out.left(128);
}
