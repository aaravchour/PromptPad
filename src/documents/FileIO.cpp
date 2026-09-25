#include "documents/FileIO.h"
#include "AppIdentity.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

namespace FileIO {

bool validUtf8Text(const QByteArray &bytes) {
    const auto *data = reinterpret_cast<const unsigned char *>(bytes.constData());
    const qsizetype size = bytes.size();
    for (qsizetype i = 0; i < size;) {
        const unsigned char first = data[i];
        if (first == 0) return false;
        if (first < 0x80) { ++i; continue; }
        int count = 0;
        unsigned int codepoint = 0;
        if (first >= 0xC2 && first <= 0xDF) { count = 2; codepoint = first & 0x1F; }
        else if (first >= 0xE0 && first <= 0xEF) { count = 3; codepoint = first & 0x0F; }
        else if (first >= 0xF0 && first <= 0xF4) { count = 4; codepoint = first & 0x07; }
        else return false;
        if (i + count > size) return false;
        for (int j = 1; j < count; ++j) {
            if ((data[i + j] & 0xC0) != 0x80) return false;
            codepoint = (codepoint << 6) | (data[i + j] & 0x3F);
        }
        if ((count == 2 && codepoint < 0x80) ||
            (count == 3 && codepoint < 0x800) ||
            (count == 4 && codepoint < 0x10000) ||
            (codepoint >= 0xD800 && codepoint <= 0xDFFF) ||
            codepoint > 0x10FFFF) return false;
        i += count;
    }
    return true;
}

Result read(const QString &path) {
    Result result;
    QFileInfo info(path);
    if (!info.exists() || !info.isFile()) {
        result.error = QStringLiteral("File does not exist or is not a regular file.");
        return result;
    }
    result.canonicalPath = info.canonicalFilePath();
    QFile file(result.canonicalPath);
    if (!file.open(QIODevice::ReadOnly)) {
        result.error = file.errorString();
        return result;
    }
    const QByteArray raw = file.readAll();
    if (file.error() != QFileDevice::NoError) {
        result.error = file.errorString();
        return result;
    }
    result.sha256 = QCryptographicHash::hash(raw, QCryptographicHash::Sha256);
    result.utf8Bom = raw.startsWith("\xEF\xBB\xBF");
    result.text = result.utf8Bom ? raw.mid(3) : raw;
    if (!validUtf8Text(result.text)) {
        result.error = QStringLiteral("This file is binary or is not valid UTF-8. It was not opened for editing.");
        result.text.clear();
        return result;
    }
    const auto crlf = result.text.count("\r\n");
    const auto lf = result.text.count('\n') - crlf;
    const auto cr = result.text.count('\r') - crlf;
    result.eolMode = crlf > lf && crlf >= cr ? 0 : cr > lf ? 1 : 2;
    result.ok = true;
    return result;
}

Result save(const QString &path, const QByteArray &text, bool utf8Bom,
            const QByteArray &expectedSha256, bool expectAbsent) {
    Result result;
    if (!validUtf8Text(text)) {
        result.error = QStringLiteral("The document contains invalid UTF-8 or a NUL byte.");
        return result;
    }
    const QFileInfo info(path);
    if (info.isSymLink()) {
        result.error = QStringLiteral("Saving through a symbolic link is not supported. Open its target or choose another path.");
        return result;
    }
    if (info.exists() && !info.isWritable()) {
        result.error = QStringLiteral("The destination file is read-only.");
        return result;
    }
    if (!QFileInfo(info.absolutePath()).isDir()) {
        result.error = QStringLiteral("The destination directory does not exist.");
        return result;
    }
    if (expectAbsent && info.exists()) {
        result.conflict = true;
        result.error = QStringLiteral("A file now exists at this path.");
        return result;
    }
    if (!expectedSha256.isEmpty()) {
        if (!info.exists()) {
            result.conflict = true;
            result.error = QStringLiteral("The file has been removed outside %1.").arg(AppIdentity::displayName());
            return result;
        }
        QFile current(path);
        if (!current.open(QIODevice::ReadOnly)) {
            result.error = current.errorString();
            return result;
        }
        if (QCryptographicHash::hash(current.readAll(), QCryptographicHash::Sha256) != expectedSha256) {
            result.conflict = true;
            result.error = QStringLiteral("The file changed outside %1.").arg(AppIdentity::displayName());
            return result;
        }
    }
    QByteArray raw;
    raw.reserve(text.size() + (utf8Bom ? 3 : 0));
    if (utf8Bom) raw.append("\xEF\xBB\xBF", 3);
    raw.append(text);
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly)) {
        result.error = file.errorString();
        return result;
    }
    if (file.write(raw) != raw.size()) {
        result.error = file.errorString();
        file.cancelWriting();
        return result;
    }
    // Recheck immediately before the atomic replacement. This narrows the
    // conflict race, although a concurrent writer can still win afterwards.
    if (!expectedSha256.isEmpty()) {
        QFile current(path);
        if (!current.open(QIODevice::ReadOnly) ||
            QCryptographicHash::hash(current.readAll(), QCryptographicHash::Sha256) != expectedSha256) {
            result.conflict = true;
            result.error = QStringLiteral("The file changed while saving.");
            file.cancelWriting();
            return result;
        }
    }
    if (!file.commit()) {
        result.error = file.errorString();
        return result;
    }
    result.ok = true;
    result.canonicalPath = QFileInfo(path).canonicalFilePath();
    result.sha256 = QCryptographicHash::hash(raw, QCryptographicHash::Sha256);
    result.utf8Bom = utf8Bom;
    result.text = text;
    return result;
}

}
