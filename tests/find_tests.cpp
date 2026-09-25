#include "documents/FindReplace.h"

#include <QTest>

class FindTests : public QObject {
    Q_OBJECT
private slots:
    void unicodeBytesAndWholeWords() {
        const QByteArray source = QString::fromUtf8("αα cat scatter cat 🧭 cat").toUtf8();
        const auto found = FindReplace::find(source, QStringLiteral("cat"), QStringLiteral("dog"),
                                              false, true, true, 0, source.size());
        QCOMPARE(found.matches.size(), 3);
        QCOMPARE(source.mid(found.matches[0].start, found.matches[0].end - found.matches[0].start), QByteArray("cat"));
        QCOMPARE(found.matches[0].start, qsizetype(5));
    }
    void invalidAndEmptyRegex() {
        const QByteArray source("abc\n");
        QVERIFY(!FindReplace::find(source, QStringLiteral("["), {}, true, true, false, 0, source.size()).error.isEmpty());
        const auto empty = FindReplace::find(source, QStringLiteral("(?=b)"), {}, true, true, false, 0, source.size());
        QCOMPARE(empty.matches.size(), 1);
        QCOMPARE(empty.matches[0].start, empty.matches[0].end);
    }
};

QTEST_MAIN(FindTests)
#include "find_tests.moc"
