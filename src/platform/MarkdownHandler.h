#pragma once

#include <QString>

#include <functional>

// The system's default app for Markdown files. Only macOS is implemented; on
// other platforms supported() is false and the rest are no-ops.
namespace MarkdownHandler {

bool supported();
// True when this app bundle is the default for .md files.
bool isDefault();
// Asks the system to make this app the default for .md files. macOS may show
// its own confirmation first. The callback runs on the GUI thread.
void makeDefault(std::function<void(bool ok, const QString &error)> done);

}
