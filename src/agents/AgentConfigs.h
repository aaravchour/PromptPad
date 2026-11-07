#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

// Finds the Markdown files that locally installed coding agents read as
// instructions (global rules, personas, memories, sub-agents, commands and
// skills) so they can be opened and edited like any other prompt.
// Detection is read-only: it only lists files. Nothing is created or changed
// until the user saves a file from the editor.
namespace AgentConfigs {

struct File {
    QString path;          // absolute path
    QString relativePath;  // shown in the UI, relative to the agent root
    bool exists = true;    // false for a well-known file the agent would read but that has not been created yet
};

struct Agent {
    QString id;
    QString name;
    QString root;
    bool custom = false;
    QVector<File> files;
};

struct Location {
    QString relativePath;
    bool recursive = false;
};

struct Definition {
    QString id;
    QString name;
    QString root;                 // relative to the home directory
    QStringList primaryFiles;     // offered even when missing, so they can be created
    QVector<Location> locations;  // files or folders to scan for Markdown
};

// The built-in list of known agents and where they keep their Markdown.
const QVector<Definition> &definitions();

// Every agent whose folder exists under `home`, plus each folder in
// `customFolders` (scanned recursively). Files are sorted by relative path.
QVector<Agent> detect(const QString &home, const QStringList &customFolders = {});

// Scans one definition. Returns an agent with no files if its root is missing.
Agent scan(const Definition &definition, const QString &home);

// Whether a file name looks like a Markdown instruction file.
bool isMarkdown(const QString &fileName);

}  // namespace AgentConfigs
