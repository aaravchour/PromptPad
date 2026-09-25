#include "documents/FileIO.h"
#include "storage/Store.h"
#include "editor/PromptEditor.h"
#include "ui/MainWindow.h"

#include <QFile>
#include <QProcess>
#include <QSettings>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

class PreservationTests : public QObject {
    Q_OBJECT
private slots:
    void utf8BomLineEndingsAndConflict() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString path = temp.filePath(QStringLiteral("Café 🧭.md"));
        const QByteArray text = QString::fromUtf8("# Café 🧭\r\n中文 {{braces}}\r\n").toUtf8();
        const auto first = FileIO::save(path, text, true, {}, true);
        QVERIFY2(first.ok, qPrintable(first.error));
        const auto loaded = FileIO::read(path);
        QVERIFY2(loaded.ok, qPrintable(loaded.error));
        QCOMPARE(loaded.text, text);
        QVERIFY(loaded.utf8Bom);
        QCOMPARE(loaded.eolMode, 0);
        QFile raw(path);
        QVERIFY(raw.open(QIODevice::ReadOnly));
        QCOMPARE(raw.readAll(), QByteArray("\xEF\xBB\xBF", 3) + text);
        raw.close();
        QVERIFY(raw.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(raw.write("external"), qint64(8));
        raw.close();
        const auto conflict = FileIO::save(path, text, true, loaded.sha256);
        QVERIFY(conflict.conflict);
        QVERIFY(!conflict.ok);
        QVERIFY(raw.open(QIODevice::ReadOnly));
        QCOMPARE(raw.readAll(), QByteArray("external"));
    }
    void rejectsInvalidAndMissingDestination() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QVERIFY(!FileIO::validUtf8Text(QByteArray("x\0y", 3)));
        QVERIFY(!FileIO::validUtf8Text(QByteArray("\xC3\x28", 2)));
        const auto result = FileIO::save(temp.filePath("missing/doc.md"), "data", false, {}, true);
        QVERIFY(!result.ok);
        const QString invalidPath = temp.filePath(QStringLiteral("invalid.md"));
        QFile invalid(invalidPath);
        QVERIFY(invalid.open(QIODevice::WriteOnly));
        QCOMPARE(invalid.write(QByteArray("\xC3\x28", 2)), qint64(2));
        invalid.close();
        QVERIFY(!FileIO::read(invalidPath).ok);
#ifdef Q_OS_UNIX
        const QString targetPath = temp.filePath(QStringLiteral("target.md"));
        QVERIFY(FileIO::save(targetPath, "original", false, {}, true).ok);
        const QString linkPath = temp.filePath(QStringLiteral("link.md"));
        QVERIFY(QFile::link(targetPath, linkPath));
        const auto linkSave = FileIO::save(linkPath, "replacement", false, {}, false);
        QVERIFY(!linkSave.ok);
        QCOMPARE(FileIO::read(targetPath).text, QByteArray("original"));
#endif
    }
    void recoveryAndMigration() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QString error;
        {
            Store store;
            QVERIFY2(store.open(temp.path(), &error), qPrintable(error));
            DraftRecord draft;
            draft.id = QStringLiteral("stable-id");
            draft.title = QStringLiteral("New prompt");
            draft.content = QString::fromUtf8("中文 🧭\n").toUtf8();
            draft.modified = true;
            draft.open = true;
            draft.caret = 5;
            QVERIFY2(store.saveDraft(draft, &error), qPrintable(error));
            QVERIFY2(store.saveCheckpoint(draft.id, QStringLiteral("Before edit"), draft.content, &error), qPrintable(error));
        }
        {
            Store store;
            QVERIFY2(store.open(temp.path(), &error), qPrintable(error));
            QCOMPARE(store.listDrafts(true).size(), 1);
            const auto loaded = store.loadDraft(QStringLiteral("stable-id"));
            QCOMPARE(loaded.content, QString::fromUtf8("中文 🧭\n").toUtf8());
            QCOMPARE(loaded.caret, 5);
            QCOMPARE(store.checkpoints(loaded.id).size(), 1);
        }
    }
    void legacyDraftMigratesToFoldersAndTemplates() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        {
            QSqlDatabase seed = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("legacy-seed"));
            seed.setDatabaseName(temp.filePath(QStringLiteral("promptpad.sqlite")));
            QVERIFY(seed.open());
            QSqlQuery query(seed);
            QVERIFY(query.exec(QStringLiteral("CREATE TABLE drafts (id TEXT PRIMARY KEY, path TEXT, title TEXT NOT NULL, content BLOB, saved_hash BLOB, bom INTEGER NOT NULL DEFAULT 0, modified INTEGER NOT NULL DEFAULT 0, starred INTEGER NOT NULL DEFAULT 0, is_open INTEGER NOT NULL DEFAULT 0, caret INTEGER NOT NULL DEFAULT 0, first_line INTEGER NOT NULL DEFAULT 0, tab_order INTEGER NOT NULL DEFAULT 0, updated_at INTEGER NOT NULL)")));
            QVERIFY(query.exec(QStringLiteral("INSERT INTO drafts (id,title,content,modified,updated_at) VALUES ('old','Legacy prompt',X'236120F09FA7AD',1,1)")));
            QVERIFY(query.exec(QStringLiteral("PRAGMA user_version=1")));
            query = QSqlQuery{};
            seed.close();
            seed = QSqlDatabase{};
        }
        QSqlDatabase::removeDatabase(QStringLiteral("legacy-seed"));
        QString error;
        {
            Store store;
            QVERIFY2(store.open(temp.path(), &error), qPrintable(error));
            QCOMPARE(store.loadDraft(QStringLiteral("old")).content, QByteArray::fromHex("236120F09FA7AD"));
            QVERIFY2(store.saveFolder({QStringLiteral("folder"), QStringLiteral("Writing")}, &error), qPrintable(error));
            QVERIFY2(store.setDraftFolder(QStringLiteral("old"), QStringLiteral("folder"), &error), qPrintable(error));
            const QByteArray templateSource = QString::fromUtf8("# Café 🧭\n{{literal}}\n").toUtf8();
            QVERIFY2(store.saveTemplate({QStringLiteral("template"), QStringLiteral("Reusable"),
                QStringLiteral("folder"), templateSource, 0}, &error), qPrintable(error));
            QCOMPARE(store.loadTemplate(QStringLiteral("template")).content, templateSource);
        }
        {
            Store store;
            QVERIFY2(store.open(temp.path(), &error), qPrintable(error));
            QCOMPARE(store.loadDraft(QStringLiteral("old")).folderId, QStringLiteral("folder"));
            QCOMPARE(store.templates().size(), 1);
            QVERIFY2(store.removeFolder(QStringLiteral("folder"), &error), qPrintable(error));
            QCOMPARE(store.loadDraft(QStringLiteral("old")).folderId, QString());
            QCOMPARE(store.loadTemplate(QStringLiteral("template")).folderId, QString());
            QCOMPARE(store.loadDraft(QStringLiteral("old")).content, QByteArray::fromHex("236120F09FA7AD"));
        }
    }
    void recoversAfterForcedExit() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QProcess worker;
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
        worker.setProcessEnvironment(environment);
        worker.start(QStringLiteral(RECOVERY_WORKER_EXE), {temp.path()});
        QVERIFY(worker.waitForStarted());
        QVERIFY(worker.waitForFinished(5000));
        QCOMPARE(worker.exitCode(), 73);
        QString error;
        Store store;
        QVERIFY2(store.open(temp.path(), &error), qPrintable(error));
        const auto drafts = store.listDrafts(true);
        QCOMPARE(drafts.size(), 1);
        const auto recovered = store.loadDraft(drafts[0].id);
        QCOMPARE(recovered.content, QString::fromUtf8("# Recovery\nα🧭 {{braces}}\n").toUtf8());
        QCOMPARE(recovered.caret, 4);
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temp.path());
        MainWindow window(&store);
        auto *editor = window.findChild<PromptEditor *>();
        QVERIFY(editor);
        QCOMPARE(editor->textUtf8(), recovered.content);
        QCOMPARE(editor->send(SCI_GETCURRENTPOS), sptr_t(4));
    }
    void rejectsCorruptMetadataWithoutErasingRecovery() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QFile file(temp.filePath(QStringLiteral("promptpad.sqlite")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("not a SQLite database");
        file.close();
        Store store;
        QString error;
        QVERIFY(!store.open(temp.path(), &error));
        QVERIFY(error.contains(QStringLiteral("damaged")) || error.contains(QStringLiteral("database"), Qt::CaseInsensitive));
        QVERIFY(file.exists());
    }
};

QTEST_MAIN(PreservationTests)
#include "preservation_tests.moc"
