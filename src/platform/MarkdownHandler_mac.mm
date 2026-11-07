#include "platform/MarkdownHandler.h"

#include <QCoreApplication>
#include <QMetaObject>

#import <AppKit/AppKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

namespace {

API_AVAILABLE(macos(11.0)) UTType *markdownType() {
    if (UTType *type = [UTType typeWithIdentifier:@"net.daringfireball.markdown"]) return type;
    return [UTType typeWithFilenameExtension:@"md"];
}

}

namespace MarkdownHandler {

bool supported() {
    if (@available(macOS 12.0, *)) return [[NSBundle mainBundle] bundleIdentifier] != nil;
    return false;
}

bool isDefault() {
    if (@available(macOS 12.0, *)) {
        @autoreleasepool {
            NSString *ownId = [[NSBundle mainBundle] bundleIdentifier];
            UTType *type = markdownType();
            if (!ownId || !type) return false;
            NSURL *handler = [[NSWorkspace sharedWorkspace] URLForApplicationToOpenContentType:type];
            if (!handler) return false;
            NSString *handlerId = [[NSBundle bundleWithURL:handler] bundleIdentifier];
            return handlerId && [handlerId caseInsensitiveCompare:ownId] == NSOrderedSame;
        }
    }
    return false;
}

void makeDefault(std::function<void(bool, const QString &)> done) {
    auto finish = [done](bool ok, const QString &error) {
        if (!done) return;
        QMetaObject::invokeMethod(QCoreApplication::instance(), [done, ok, error] { done(ok, error); }, Qt::QueuedConnection);
    };
    if (@available(macOS 12.0, *)) {
        @autoreleasepool {
            UTType *type = markdownType();
            NSURL *bundle = [[NSBundle mainBundle] bundleURL];
            if (!type || ![[NSBundle mainBundle] bundleIdentifier]) {
                finish(false, QStringLiteral("This copy of PromptPad is not running from an app bundle."));
                return;
            }
            [[NSWorkspace sharedWorkspace] setDefaultApplicationAtURL:bundle
                                                    toOpenContentType:type
                                                    completionHandler:^(NSError *error) {
                if (error) finish(false, QString::fromNSString(error.localizedDescription));
                else finish(true, {});
            }];
            return;
        }
    }
    finish(false, QStringLiteral("Choosing a default app needs macOS 12 or later."));
}

}
