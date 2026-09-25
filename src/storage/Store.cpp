#include "storage/Store.h"
#include "AppIdentity.h"

#include "documents/FileIO.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

Store::Store() : connectionName_(QUuid::createUuid().toString(QUuid::WithoutBraces)) {}
Store::~Store() {
    if (db_.isValid()) {
        db_.close();
        db_ = {};
        QSqlDatabase::removeDatabase(connectionName_);
    }
}

void Store::setError(QString *target, const QString &value) { if (target) *target = value; }

bool Store::open(const QString &directory, QString *error) {
    if (!QDir().mkpath(directory)) {
        setError(error, QStringLiteral("Could not create the local data directory."));
        return false;
    }
    directory_ = directory;
    QFile::setPermissions(directory, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    db_ = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName_);
    db_.setDatabaseName(QDir(directory).filePath(QStringLiteral("promptpad.sqlite")));
    if (!db_.open()) {
        setError(error, db_.lastError().text());
        return false;
    }
    QFile::setPermissions(db_.databaseName(), QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    QSqlQuery pragmas(db_);
    if (!pragmas.exec(QStringLiteral("PRAGMA foreign_keys=ON")) ||
        !pragmas.exec(QStringLiteral("PRAGMA synchronous=FULL"))) {
        setError(error, pragmas.lastError().text());
        return false;
    }
    QSqlQuery integrity(db_);
    if (!integrity.exec(QStringLiteral("PRAGMA quick_check")) || !integrity.next() ||
        integrity.value(0).toString() != QStringLiteral("ok")) {
        setError(error, QStringLiteral("The local database is damaged. Raw draft backups remain in %1/recovery.").arg(directory));
        return false;
    }
    QSqlQuery version(db_);
    if (!version.exec(QStringLiteral("PRAGMA user_version")) || !version.next()) {
        setError(error, version.lastError().text());
        return false;
    }
    int current = version.value(0).toInt();
    if (current > 2) {
        setError(error, QStringLiteral("This data folder was created by a newer %1 version.").arg(AppIdentity::displayName()));
        return false;
    }
    if (current == 0) {
        if (!db_.transaction()) { setError(error, db_.lastError().text()); return false; }
        const QStringList statements = {
            QStringLiteral("CREATE TABLE drafts (id TEXT PRIMARY KEY, path TEXT, title TEXT NOT NULL, content BLOB, saved_hash BLOB, bom INTEGER NOT NULL DEFAULT 0, modified INTEGER NOT NULL DEFAULT 0, starred INTEGER NOT NULL DEFAULT 0, is_open INTEGER NOT NULL DEFAULT 0, caret INTEGER NOT NULL DEFAULT 0, first_line INTEGER NOT NULL DEFAULT 0, tab_order INTEGER NOT NULL DEFAULT 0, updated_at INTEGER NOT NULL)"),
            QStringLiteral("CREATE INDEX drafts_updated ON drafts(updated_at DESC)"),
            QStringLiteral("CREATE TABLE checkpoints (id INTEGER PRIMARY KEY AUTOINCREMENT, draft_id TEXT NOT NULL, name TEXT NOT NULL, created_at INTEGER NOT NULL, content BLOB NOT NULL, FOREIGN KEY(draft_id) REFERENCES drafts(id) ON DELETE CASCADE)"),
            QStringLiteral("CREATE INDEX checkpoints_draft ON checkpoints(draft_id, created_at DESC)"),
            QStringLiteral("CREATE TABLE snippets (id TEXT PRIMARY KEY, title TEXT NOT NULL, path TEXT NOT NULL)"),
            QStringLiteral("PRAGMA user_version=1")
        };
        for (const QString &statement : statements) {
            QSqlQuery query(db_);
            if (!query.exec(statement)) { setError(error, query.lastError().text()); db_.rollback(); return false; }
        }
        if (!db_.commit()) { setError(error, db_.lastError().text()); return false; }
        current = 1;
    }
    if (current == 1) {
        if (!db_.transaction()) { setError(error, db_.lastError().text()); return false; }
        const QStringList statements = {
            QStringLiteral("CREATE TABLE folders (id TEXT PRIMARY KEY, title TEXT NOT NULL UNIQUE COLLATE NOCASE)"),
            QStringLiteral("ALTER TABLE drafts ADD COLUMN folder_id TEXT REFERENCES folders(id) ON DELETE SET NULL"),
            QStringLiteral("CREATE INDEX drafts_folder ON drafts(folder_id)"),
            QStringLiteral("CREATE TABLE templates (id TEXT PRIMARY KEY, title TEXT NOT NULL, folder_id TEXT REFERENCES folders(id) ON DELETE SET NULL, content BLOB NOT NULL, updated_at INTEGER NOT NULL)"),
            QStringLiteral("CREATE INDEX templates_updated ON templates(updated_at DESC)"),
            QStringLiteral("PRAGMA user_version=2")
        };
        for (const QString &statement : statements) {
            QSqlQuery query(db_);
            if (!query.exec(statement)) { setError(error, query.lastError().text()); db_.rollback(); return false; }
        }
        if (!db_.commit()) { setError(error, db_.lastError().text()); return false; }
    }
    return true;
}

bool Store::saveDraft(const DraftRecord &draft, QString *error) {
    const QString recoveryDirectory = QDir(directory_).filePath(QStringLiteral("recovery"));
    const QString recoveryPath = QDir(recoveryDirectory).filePath(draft.id + QStringLiteral(".md"));
    if (draft.modified || draft.path.isEmpty()) {
        if (!QDir().mkpath(recoveryDirectory)) {
            setError(error, QStringLiteral("Could not create recovery directory.")); return false;
        }
        QFile::setPermissions(recoveryDirectory, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        QSaveFile backup(recoveryPath);
        backup.setDirectWriteFallback(false);
        if (!backup.open(QIODevice::WriteOnly) || backup.write(draft.content) != draft.content.size() || !backup.commit()) {
            setError(error, backup.errorString()); return false;
        }
        QFile::setPermissions(recoveryPath, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    }
    QSqlQuery query(db_);
    query.prepare(QStringLiteral("INSERT INTO drafts (id,path,title,content,saved_hash,bom,modified,starred,is_open,caret,first_line,tab_order,updated_at,folder_id) VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?) ON CONFLICT(id) DO UPDATE SET path=excluded.path,title=excluded.title,content=excluded.content,saved_hash=excluded.saved_hash,bom=excluded.bom,modified=excluded.modified,starred=excluded.starred,is_open=excluded.is_open,caret=excluded.caret,first_line=excluded.first_line,tab_order=excluded.tab_order,updated_at=excluded.updated_at,folder_id=excluded.folder_id"));
    query.addBindValue(draft.id);
    query.addBindValue(draft.path);
    query.addBindValue(draft.title);
    query.addBindValue(draft.content);
    query.addBindValue(draft.savedHash);
    query.addBindValue(draft.bom ? 1 : 0);
    query.addBindValue(draft.modified ? 1 : 0);
    query.addBindValue(draft.starred ? 1 : 0);
    query.addBindValue(draft.open ? 1 : 0);
    query.addBindValue(draft.caret);
    query.addBindValue(draft.firstVisibleLine);
    query.addBindValue(draft.tabOrder);
    query.addBindValue(QDateTime::currentSecsSinceEpoch());
    query.addBindValue(draft.folderId.isEmpty() ? QVariant() : QVariant(draft.folderId));
    if (!query.exec()) { setError(error, query.lastError().text()); return false; }
    if (!draft.modified && !draft.path.isEmpty()) QFile::remove(recoveryPath);
    return true;
}

bool Store::removeDraft(const QString &id, QString *error) {
    QSqlQuery query(db_);
    query.prepare(QStringLiteral("DELETE FROM drafts WHERE id=?"));
    query.addBindValue(id);
    if (!query.exec()) { setError(error, query.lastError().text()); return false; }
    QFile::remove(QDir(directory_).filePath(QStringLiteral("recovery/") + id + QStringLiteral(".md")));
    return true;
}

bool Store::setOpen(const QString &id, bool open, QString *error) {
    QSqlQuery query(db_);
    query.prepare(QStringLiteral("UPDATE drafts SET is_open=? WHERE id=?"));
    query.addBindValue(open ? 1 : 0);
    query.addBindValue(id);
    if (!query.exec()) { setError(error, query.lastError().text()); return false; }
    return true;
}

QVector<DraftRecord> Store::listDrafts(bool onlyOpen, const QString &search) const {
    QVector<DraftRecord> output;
    QSqlQuery query(db_);
    QString statement = QStringLiteral("SELECT id,path,title,saved_hash,bom,modified,starred,is_open,caret,first_line,tab_order,updated_at,folder_id FROM drafts WHERE 1=1");
    if (onlyOpen) statement += QStringLiteral(" AND is_open=1");
    if (!search.isEmpty()) statement += QStringLiteral(" AND (title LIKE ? OR CAST(content AS TEXT) LIKE ?)");
    statement += onlyOpen ? QStringLiteral(" ORDER BY tab_order") : QStringLiteral(" ORDER BY updated_at DESC LIMIT 200");
    query.prepare(statement);
    if (!search.isEmpty()) {
        const QString pattern = QStringLiteral("%") + search + QStringLiteral("%");
        query.addBindValue(pattern);
        query.addBindValue(pattern);
    }
    if (!query.exec()) return output;
    while (query.next()) {
        DraftRecord d;
        d.id = query.value(0).toString(); d.path = query.value(1).toString(); d.title = query.value(2).toString();
        d.savedHash = query.value(3).toByteArray(); d.bom = query.value(4).toBool(); d.modified = query.value(5).toBool();
        d.starred = query.value(6).toBool(); d.open = query.value(7).toBool(); d.caret = query.value(8).toInt();
        d.firstVisibleLine = query.value(9).toInt(); d.tabOrder = query.value(10).toInt(); d.updatedAt = query.value(11).toLongLong();
        d.folderId = query.value(12).toString();
        output.append(d);
    }
    return output;
}

DraftRecord Store::loadDraft(const QString &id) const {
    QSqlQuery query(db_);
    query.prepare(QStringLiteral("SELECT id,path,title,content,saved_hash,bom,modified,starred,is_open,caret,first_line,tab_order,updated_at,folder_id FROM drafts WHERE id=?"));
    query.addBindValue(id);
    DraftRecord d;
    if (query.exec() && query.next()) {
        d.id = query.value(0).toString(); d.path = query.value(1).toString(); d.title = query.value(2).toString();
        d.content = query.value(3).toByteArray(); d.savedHash = query.value(4).toByteArray();
        d.bom = query.value(5).toBool(); d.modified = query.value(6).toBool(); d.starred = query.value(7).toBool();
        d.open = query.value(8).toBool(); d.caret = query.value(9).toInt(); d.firstVisibleLine = query.value(10).toInt();
        d.tabOrder = query.value(11).toInt(); d.updatedAt = query.value(12).toLongLong();
        d.folderId = query.value(13).toString();
    }
    if (!d.id.isEmpty() && (d.modified || d.path.isEmpty())) {
        QFile backup(QDir(directory_).filePath(QStringLiteral("recovery/") + id + QStringLiteral(".md")));
        if (backup.open(QIODevice::ReadOnly)) {
            const QByteArray content = backup.readAll();
            if (FileIO::validUtf8Text(content)) d.content = content;
        }
    }
    return d;
}

bool Store::saveCheckpoint(const QString &draftId, const QString &name, const QByteArray &content, QString *error) {
    if (!db_.transaction()) { setError(error, db_.lastError().text()); return false; }
    QSqlQuery query(db_);
    query.prepare(QStringLiteral("INSERT INTO checkpoints(draft_id,name,created_at,content) VALUES(?,?,?,?)"));
    query.addBindValue(draftId); query.addBindValue(name); query.addBindValue(QDateTime::currentSecsSinceEpoch()); query.addBindValue(content);
    if (!query.exec()) { setError(error, query.lastError().text()); db_.rollback(); return false; }
    QSqlQuery prune(db_);
    prune.prepare(QStringLiteral("DELETE FROM checkpoints WHERE draft_id=? AND id NOT IN (SELECT id FROM checkpoints WHERE draft_id=? ORDER BY id DESC LIMIT 20)"));
    prune.addBindValue(draftId); prune.addBindValue(draftId);
    if (!prune.exec()) { setError(error, prune.lastError().text()); db_.rollback(); return false; }
    if (!db_.commit()) { setError(error, db_.lastError().text()); return false; }
    return true;
}

QVector<CheckpointRecord> Store::checkpoints(const QString &draftId) const {
    QVector<CheckpointRecord> output;
    QSqlQuery query(db_);
    query.prepare(QStringLiteral("SELECT id,name,created_at FROM checkpoints WHERE draft_id=? ORDER BY id DESC"));
    query.addBindValue(draftId);
    if (!query.exec()) return output;
    while (query.next()) output.append({query.value(0).toLongLong(), query.value(1).toString(), query.value(2).toLongLong(), {}});
    return output;
}

QByteArray Store::loadCheckpoint(qint64 id) const {
    QSqlQuery query(db_);
    query.prepare(QStringLiteral("SELECT content FROM checkpoints WHERE id=?"));
    query.addBindValue(id);
    return query.exec() && query.next() ? query.value(0).toByteArray() : QByteArray{};
}

bool Store::clearHistory(QString *error) {
    QSqlQuery query(db_);
    if (!query.exec(QStringLiteral("DELETE FROM checkpoints"))) { setError(error, query.lastError().text()); return false; }
    return true;
}

bool Store::clearClosedDrafts(QString *error) {
    QSqlQuery ids(db_);
    if (!ids.exec(QStringLiteral("SELECT id FROM drafts WHERE is_open=0"))) {
        setError(error, ids.lastError().text()); return false;
    }
    QStringList removed;
    while (ids.next()) removed.append(ids.value(0).toString());
    QSqlQuery query(db_);
    if (!query.exec(QStringLiteral("DELETE FROM drafts WHERE is_open=0"))) {
        setError(error, query.lastError().text()); return false;
    }
    for (const QString &id : removed)
        QFile::remove(QDir(directory_).filePath(QStringLiteral("recovery/") + id + QStringLiteral(".md")));
    return true;
}

bool Store::saveSnippet(const SnippetRecord &snippet, QString *error) {
    QSqlQuery query(db_);
    query.prepare(QStringLiteral("INSERT INTO snippets(id,title,path) VALUES(?,?,?) ON CONFLICT(id) DO UPDATE SET title=excluded.title,path=excluded.path"));
    query.addBindValue(snippet.id); query.addBindValue(snippet.title); query.addBindValue(snippet.path);
    if (!query.exec()) { setError(error, query.lastError().text()); return false; }
    return true;
}

QVector<SnippetRecord> Store::snippets() const {
    QVector<SnippetRecord> output;
    QSqlQuery query(db_);
    if (!query.exec(QStringLiteral("SELECT id,title,path FROM snippets ORDER BY title COLLATE NOCASE"))) return output;
    while (query.next()) output.append({query.value(0).toString(), query.value(1).toString(), query.value(2).toString()});
    return output;
}

bool Store::removeSnippet(const QString &id, QString *error) {
    QSqlQuery query(db_);
    query.prepare(QStringLiteral("DELETE FROM snippets WHERE id=?"));
    query.addBindValue(id);
    if (!query.exec()) { setError(error, query.lastError().text()); return false; }
    return true;
}

bool Store::saveFolder(const FolderRecord &folder, QString *error) {
    QSqlQuery query(db_);
    query.prepare(QStringLiteral("INSERT INTO folders(id,title) VALUES(?,?) ON CONFLICT(id) DO UPDATE SET title=excluded.title"));
    query.addBindValue(folder.id);
    query.addBindValue(folder.title);
    if (!query.exec()) { setError(error, query.lastError().text()); return false; }
    return true;
}

bool Store::removeFolder(const QString &id, QString *error) {
    QSqlQuery query(db_);
    query.prepare(QStringLiteral("DELETE FROM folders WHERE id=?"));
    query.addBindValue(id);
    if (!query.exec()) { setError(error, query.lastError().text()); return false; }
    return true;
}

QVector<FolderRecord> Store::folders() const {
    QVector<FolderRecord> output;
    QSqlQuery query(db_);
    if (!query.exec(QStringLiteral("SELECT id,title FROM folders ORDER BY title COLLATE NOCASE"))) return output;
    while (query.next()) output.append({query.value(0).toString(), query.value(1).toString()});
    return output;
}

bool Store::setDraftFolder(const QString &id, const QString &folderId, QString *error) {
    QSqlQuery query(db_);
    query.prepare(QStringLiteral("UPDATE drafts SET folder_id=? WHERE id=?"));
    query.addBindValue(folderId.isEmpty() ? QVariant() : QVariant(folderId));
    query.addBindValue(id);
    if (!query.exec()) { setError(error, query.lastError().text()); return false; }
    return true;
}

bool Store::saveTemplate(const TemplateRecord &item, QString *error) {
    QSqlQuery query(db_);
    query.prepare(QStringLiteral("INSERT INTO templates(id,title,folder_id,content,updated_at) VALUES(?,?,?,?,?) ON CONFLICT(id) DO UPDATE SET title=excluded.title,folder_id=excluded.folder_id,content=excluded.content,updated_at=excluded.updated_at"));
    query.addBindValue(item.id);
    query.addBindValue(item.title);
    query.addBindValue(item.folderId.isEmpty() ? QVariant() : QVariant(item.folderId));
    query.addBindValue(item.content);
    query.addBindValue(QDateTime::currentSecsSinceEpoch());
    if (!query.exec()) { setError(error, query.lastError().text()); return false; }
    return true;
}

bool Store::removeTemplate(const QString &id, QString *error) {
    QSqlQuery query(db_);
    query.prepare(QStringLiteral("DELETE FROM templates WHERE id=?"));
    query.addBindValue(id);
    if (!query.exec()) { setError(error, query.lastError().text()); return false; }
    return true;
}

bool Store::setTemplateFolder(const QString &id, const QString &folderId, QString *error) {
    QSqlQuery query(db_);
    query.prepare(QStringLiteral("UPDATE templates SET folder_id=? WHERE id=?"));
    query.addBindValue(folderId.isEmpty() ? QVariant() : QVariant(folderId));
    query.addBindValue(id);
    if (!query.exec()) { setError(error, query.lastError().text()); return false; }
    return true;
}

QVector<TemplateRecord> Store::templates(const QString &search) const {
    QVector<TemplateRecord> output;
    QSqlQuery query(db_);
    QString statement = QStringLiteral("SELECT id,title,folder_id,updated_at FROM templates");
    if (!search.isEmpty()) statement += QStringLiteral(" WHERE title LIKE ? OR CAST(content AS TEXT) LIKE ?");
    statement += QStringLiteral(" ORDER BY updated_at DESC LIMIT 200");
    query.prepare(statement);
    if (!search.isEmpty()) {
        const QString pattern = QStringLiteral("%") + search + QStringLiteral("%");
        query.addBindValue(pattern);
        query.addBindValue(pattern);
    }
    if (!query.exec()) return output;
    while (query.next()) output.append({query.value(0).toString(), query.value(1).toString(),
                                       query.value(2).toString(), {}, query.value(3).toLongLong()});
    return output;
}

TemplateRecord Store::loadTemplate(const QString &id) const {
    QSqlQuery query(db_);
    query.prepare(QStringLiteral("SELECT id,title,folder_id,content,updated_at FROM templates WHERE id=?"));
    query.addBindValue(id);
    if (!query.exec() || !query.next()) return {};
    return {query.value(0).toString(), query.value(1).toString(), query.value(2).toString(),
            query.value(3).toByteArray(), query.value(4).toLongLong()};
}
