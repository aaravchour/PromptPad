#pragma once

#include <QByteArray>
#include <QString>

namespace FileIO {

struct Result {
    bool ok = false;
    bool conflict = false;
    QString error;
    QByteArray text;
    QByteArray sha256;
    bool utf8Bom = false;
    int eolMode = 2; // Scintilla SC_EOL_LF
    QString canonicalPath;
};

Result read(const QString &path);
Result save(const QString &path, const QByteArray &text, bool utf8Bom,
            const QByteArray &expectedSha256, bool expectAbsent = false);
bool validUtf8Text(const QByteArray &bytes);

}
