#pragma once

#include <QByteArray>
#include <QSqlDatabase>
#include <QString>
#include <QVector>

struct DraftRecord {
    QString id;
    QString path;
    QString title;
    QByteArray content;
    QByteArray savedHash;
    bool bom = false;
    bool modified = false;
    bool starred = false;
    bool open = false;
    QString folderId;
    int caret = 0;
    int firstVisibleLine = 0;
    int tabOrder = 0;
    qint64 updatedAt = 0;
};

struct CheckpointRecord {
    qint64 id = 0;
    QString name;
    qint64 createdAt = 0;
    QByteArray content;
};

struct SnippetRecord {
    QString id;
    QString title;
    QString path;
};

struct FolderRecord {
    QString id;
    QString title;
};

struct TemplateRecord {
    QString id;
    QString title;
    QString folderId;
    QByteArray content;
    qint64 updatedAt = 0;
};

class Store {
public:
    Store();
    ~Store();
    bool open(const QString &directory, QString *error);
    QString directory() const { return directory_; }
    bool saveDraft(const DraftRecord &draft, QString *error);
    bool removeDraft(const QString &id, QString *error);
    bool setOpen(const QString &id, bool open, QString *error);
    QVector<DraftRecord> listDrafts(bool onlyOpen = false, const QString &search = {}) const;
    DraftRecord loadDraft(const QString &id) const;
    bool saveCheckpoint(const QString &draftId, const QString &name, const QByteArray &content, QString *error);
    QVector<CheckpointRecord> checkpoints(const QString &draftId) const;
    QByteArray loadCheckpoint(qint64 id) const;
    bool clearHistory(QString *error);
    bool clearClosedDrafts(QString *error);
    bool saveSnippet(const SnippetRecord &snippet, QString *error);
    QVector<SnippetRecord> snippets() const;
    bool removeSnippet(const QString &id, QString *error);
    bool saveFolder(const FolderRecord &folder, QString *error);
    bool removeFolder(const QString &id, QString *error);
    QVector<FolderRecord> folders() const;
    bool setDraftFolder(const QString &id, const QString &folderId, QString *error);
    bool saveTemplate(const TemplateRecord &item, QString *error);
    bool removeTemplate(const QString &id, QString *error);
    bool setTemplateFolder(const QString &id, const QString &folderId, QString *error);
    QVector<TemplateRecord> templates(const QString &search = {}) const;
    TemplateRecord loadTemplate(const QString &id) const;

private:
    QSqlDatabase db_;
    QString connectionName_;
    QString directory_;
    static void setError(QString *target, const QString &value);
};
