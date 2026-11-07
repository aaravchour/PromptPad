#include "platform/MarkdownHandler.h"

namespace MarkdownHandler {

bool supported() { return false; }
bool isDefault() { return false; }
void makeDefault(std::function<void(bool, const QString &)> done) {
    if (done) done(false, QStringLiteral("Choosing a default app is only available on macOS."));
}

}
