#include "agents/AgentConfigs.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QMap>

#include <algorithm>

namespace AgentConfigs {
namespace {

constexpr int kMaxFilesPerAgent = 400;
constexpr int kMaxDepth = 6;

bool skippedDirectory(const QString &name) {
    return name.startsWith(QLatin1Char('.')) || name == QStringLiteral("node_modules") ||
           name == QStringLiteral("__pycache__") || name == QStringLiteral("venv");
}

void addFile(QMap<QString, File> &found, const QDir &root, const QString &absolute, bool exists) {
    if (found.size() >= kMaxFilesPerAgent) return;
    const QString relative = QDir::fromNativeSeparators(root.relativeFilePath(absolute));
    if (relative.startsWith(QStringLiteral("../"))) return;
    auto it = found.find(relative);
    if (it != found.end()) { it->exists = it->exists || exists; return; }
    found.insert(relative, File{QDir::cleanPath(absolute), relative, exists});
}

void scanFolder(QMap<QString, File> &found, const QDir &root, const QString &folder, bool recursive, int depth = 0) {
    if (found.size() >= kMaxFilesPerAgent) return;
    QDir dir(folder);
    const auto files = dir.entryInfoList(QDir::Files | QDir::Readable, QDir::Name);
    for (const QFileInfo &info : files) {
        if (isMarkdown(info.fileName())) addFile(found, root, info.absoluteFilePath(), true);
    }
    if (!recursive || depth >= kMaxDepth) return;
    const auto folders = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Readable, QDir::Name);
    for (const QFileInfo &info : folders) {
        if (info.isSymLink() || skippedDirectory(info.fileName())) continue;
        scanFolder(found, root, info.absoluteFilePath(), true, depth + 1);
    }
}

QVector<File> sorted(const QMap<QString, File> &found) {
    QVector<File> files(found.begin(), found.end());
    std::stable_sort(files.begin(), files.end(), [](const File &a, const File &b) {
        // Top-level files first, then folders, each alphabetically.
        const bool aTop = !a.relativePath.contains(QLatin1Char('/'));
        const bool bTop = !b.relativePath.contains(QLatin1Char('/'));
        if (aTop != bTop) return aTop;
        return a.relativePath.compare(b.relativePath, Qt::CaseInsensitive) < 0;
    });
    return files;
}

}  // namespace

bool isMarkdown(const QString &fileName) {
    const QString suffix = QFileInfo(fileName).suffix().toLower();
    return suffix == QStringLiteral("md") || suffix == QStringLiteral("markdown") || suffix == QStringLiteral("mdc");
}

const QVector<Definition> &definitions() {
    static const QVector<Definition> list = {
        {QStringLiteral("claude-code"), QStringLiteral("Claude Code"), QStringLiteral(".claude"),
         {QStringLiteral("CLAUDE.md")},
         {{QString(), false}, {QStringLiteral("agents"), true}, {QStringLiteral("commands"), true},
          {QStringLiteral("skills"), true}, {QStringLiteral("output-styles"), true}}},
        {QStringLiteral("codex"), QStringLiteral("Codex"), QStringLiteral(".codex"),
         {QStringLiteral("AGENTS.md")},
         {{QString(), false}, {QStringLiteral("prompts"), true}, {QStringLiteral("skills"), true}}},
        {QStringLiteral("hermes"), QStringLiteral("Hermes Agent"), QStringLiteral(".hermes"),
         {QStringLiteral("SOUL.md"), QStringLiteral("memories/MEMORY.md"), QStringLiteral("memories/USER.md")},
         {{QString(), false}, {QStringLiteral("memories"), true}, {QStringLiteral("skills"), true}}},
        {QStringLiteral("gemini"), QStringLiteral("Gemini CLI"), QStringLiteral(".gemini"),
         {QStringLiteral("GEMINI.md")},
         {{QString(), false}}},
        {QStringLiteral("cursor"), QStringLiteral("Cursor"), QStringLiteral(".cursor"),
         {},
         {{QStringLiteral("rules"), true}, {QStringLiteral("commands"), true}, {QStringLiteral("agents"), true}}},
        {QStringLiteral("opencode"), QStringLiteral("OpenCode"), QStringLiteral(".config/opencode"),
         {QStringLiteral("AGENTS.md")},
         {{QString(), false}, {QStringLiteral("agent"), true}, {QStringLiteral("agents"), true},
          {QStringLiteral("command"), true}, {QStringLiteral("commands"), true}}},
        {QStringLiteral("amp"), QStringLiteral("Amp"), QStringLiteral(".config/amp"),
         {QStringLiteral("AGENTS.md")},
         {{QString(), false}}},
        {QStringLiteral("windsurf"), QStringLiteral("Windsurf"), QStringLiteral(".codeium/windsurf"),
         {QStringLiteral("memories/global_rules.md")},
         {{QStringLiteral("memories"), true}}},
        {QStringLiteral("qwen"), QStringLiteral("Qwen Code"), QStringLiteral(".qwen"),
         {QStringLiteral("QWEN.md")},
         {{QString(), false}, {QStringLiteral("agents"), true}, {QStringLiteral("commands"), true}}},
        {QStringLiteral("kiro"), QStringLiteral("Kiro"), QStringLiteral(".kiro"),
         {},
         {{QStringLiteral("steering"), true}, {QStringLiteral("agents"), true}}},
        {QStringLiteral("cline"), QStringLiteral("Cline"), QStringLiteral("Documents/Cline"),
         {},
         {{QStringLiteral("Rules"), true}, {QStringLiteral("Workflows"), true}}},
    };
    return list;
}

Agent scan(const Definition &definition, const QString &home) {
    Agent agent;
    agent.id = definition.id;
    agent.name = definition.name;
    agent.root = QDir::cleanPath(QDir(home).filePath(definition.root));
    const QDir root(agent.root);
    if (!QFileInfo(agent.root).isDir()) return agent;

    QMap<QString, File> found;
    for (const QString &primary : definition.primaryFiles) {
        const QString path = root.filePath(primary);
        addFile(found, root, path, QFileInfo(path).isFile());
    }
    for (const Location &location : definition.locations) {
        const QString path = location.relativePath.isEmpty() ? agent.root : root.filePath(location.relativePath);
        const QFileInfo info(path);
        if (info.isFile() && isMarkdown(info.fileName())) addFile(found, root, info.absoluteFilePath(), true);
        else if (info.isDir()) scanFolder(found, root, info.absoluteFilePath(), location.recursive);
    }
    agent.files = sorted(found);
    return agent;
}

QVector<Agent> detect(const QString &home, const QStringList &customFolders) {
    QVector<Agent> agents;
    for (const Definition &definition : definitions()) {
        if (!QFileInfo(QDir(home).filePath(definition.root)).isDir()) continue;
        agents.append(scan(definition, home));
    }
    for (const QString &folder : customFolders) {
        const QFileInfo info(folder);
        if (!info.isDir()) continue;
        Agent agent;
        agent.id = QStringLiteral("custom:") + info.absoluteFilePath();
        agent.name = info.fileName().isEmpty() ? info.absoluteFilePath() : info.fileName();
        agent.root = QDir::cleanPath(info.absoluteFilePath());
        agent.custom = true;
        QMap<QString, File> found;
        scanFolder(found, QDir(agent.root), agent.root, true);
        agent.files = sorted(found);
        agents.append(agent);
    }
    return agents;
}

}  // namespace AgentConfigs
