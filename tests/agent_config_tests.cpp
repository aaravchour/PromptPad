#include "agents/AgentConfigs.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

namespace {
void write(const QString &path, const QByteArray &text = "# synthetic\n") {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(text);
}

const AgentConfigs::Agent *find(const QVector<AgentConfigs::Agent> &agents, const QString &id) {
    for (const auto &agent : agents) if (agent.id == id) return &agent;
    return nullptr;
}

QStringList relativePaths(const AgentConfigs::Agent &agent) {
    QStringList paths;
    for (const auto &file : agent.files) paths << file.relativePath;
    return paths;
}
}

class AgentConfigTests : public QObject {
    Q_OBJECT
private slots:
    void onlyInstalledAgentsAreListed() {
        QTemporaryDir home;
        QVERIFY(home.isValid());
        QVERIFY(AgentConfigs::detect(home.path()).isEmpty());
        QDir(home.path()).mkpath(QStringLiteral(".codex"));
        const auto agents = AgentConfigs::detect(home.path());
        QCOMPARE(agents.size(), 1);
        QCOMPARE(agents[0].name, QStringLiteral("Codex"));
        // The global AGENTS.md is offered so it can be created.
        QCOMPARE(agents[0].files.size(), 1);
        QCOMPARE(agents[0].files[0].relativePath, QStringLiteral("AGENTS.md"));
        QVERIFY(!agents[0].files[0].exists);
    }

    void claudeCodeScansKnownFoldersOnly() {
        QTemporaryDir home;
        const QString root = home.filePath(QStringLiteral(".claude"));
        write(root + QStringLiteral("/CLAUDE.md"));
        write(root + QStringLiteral("/agents/reviewer.md"));
        write(root + QStringLiteral("/commands/git/ship.md"));
        write(root + QStringLiteral("/skills/pdf/SKILL.md"));
        write(root + QStringLiteral("/skills/pdf/script.py"));
        write(root + QStringLiteral("/projects/abc/notes.md"));
        write(root + QStringLiteral("/plugins/cache/readme.md"));
        write(root + QStringLiteral("/skills/pdf/node_modules/x/README.md"));
        const auto agents = AgentConfigs::detect(home.path());
        const auto *claude = find(agents, QStringLiteral("claude-code"));
        QVERIFY(claude);
        QCOMPARE(relativePaths(*claude), (QStringList{
            QStringLiteral("CLAUDE.md"), QStringLiteral("agents/reviewer.md"),
            QStringLiteral("commands/git/ship.md"), QStringLiteral("skills/pdf/SKILL.md")}));
        for (const auto &file : claude->files) {
            QVERIFY(file.exists);
            QVERIFY(QFileInfo(file.path).isAbsolute());
        }
    }

    void hermesOffersPersonaAndMemories() {
        QTemporaryDir home;
        const QString root = home.filePath(QStringLiteral(".hermes"));
        write(root + QStringLiteral("/SOUL.md"));
        write(root + QStringLiteral("/config.yaml"), "model: x\n");
        write(root + QStringLiteral("/skills/research/SKILL.md"));
        const auto *hermes = find(AgentConfigs::detect(home.path()), QStringLiteral("hermes"));
        QVERIFY(hermes);
        QCOMPARE(relativePaths(*hermes), (QStringList{
            QStringLiteral("SOUL.md"), QStringLiteral("memories/MEMORY.md"),
            QStringLiteral("memories/USER.md"), QStringLiteral("skills/research/SKILL.md")}));
        QVERIFY(hermes->files[0].exists);
        QVERIFY(!hermes->files[1].exists);
    }

    void customFoldersAreScannedRecursively() {
        QTemporaryDir home;
        QTemporaryDir custom;
        write(custom.filePath(QStringLiteral("rules.mdc")));
        write(custom.filePath(QStringLiteral("deep/a/b/prompt.markdown")));
        write(custom.filePath(QStringLiteral("deep/notes.txt")), "x");
        write(custom.filePath(QStringLiteral(".git/HEAD.md")));
        const auto agents = AgentConfigs::detect(home.path(), {custom.path(), home.filePath(QStringLiteral("missing"))});
        QCOMPARE(agents.size(), 1);
        QVERIFY(agents[0].custom);
        QCOMPARE(relativePaths(agents[0]), (QStringList{
            QStringLiteral("rules.mdc"), QStringLiteral("deep/a/b/prompt.markdown")}));
    }

    void markdownSuffixes() {
        QVERIFY(AgentConfigs::isMarkdown(QStringLiteral("A.MD")));
        QVERIFY(AgentConfigs::isMarkdown(QStringLiteral("rule.mdc")));
        QVERIFY(!AgentConfigs::isMarkdown(QStringLiteral("config.toml")));
        QVERIFY(!AgentConfigs::isMarkdown(QStringLiteral("md")));
    }
};

QTEST_MAIN(AgentConfigTests)
#include "agent_config_tests.moc"
