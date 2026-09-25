#include "documents/Outline.h"

#include <QTest>

class OutlineTests : public QObject {
    Q_OBJECT
private slots:
    void headingsAndFences() {
        const QByteArray text = "# Same\nintro\n```md\n# Hidden\n```\n### Deep\ntext\n# Same\nLast\n---\nbody\n";
        const auto h = Outline::parse(text);
        QCOMPARE(h.size(), 4);
        QCOMPARE(h[0].title, QStringLiteral("Same"));
        QCOMPARE(h[1].title, QStringLiteral("Deep"));
        QCOMPARE(h[1].parent, 0);
        QCOMPARE(h[2].title, QStringLiteral("Same"));
        QCOMPARE(h[3].title, QStringLiteral("Last"));
        QCOMPARE(h[3].level, 2);
        QCOMPARE(h[0].sectionEnd, h[2].start);
        QCOMPARE(Outline::sectionAt(h, h[1].start + 2), 1);
    }
    void moveWholeSectionPreservesBytes() {
        const QByteArray source = "# First\r\n## A\r\nα\r\n\r\n## B\r\n```\r\n# literal\r\n```\r\n# End\r\n";
        const auto headings = Outline::parse(source);
        QCOMPARE(headings.size(), 4);
        const auto edit = Outline::move(source, headings, 1, true);
        QVERIFY(edit.valid);
        QByteArray moved = source;
        moved.replace(edit.start, edit.end - edit.start, edit.replacement);
        QCOMPARE(moved, QByteArray("# First\r\n## B\r\n```\r\n# literal\r\n```\r\n## A\r\nα\r\n\r\n# End\r\n"));
        QVERIFY(!Outline::move(source, headings, 0, false).valid);
    }
};

QTEST_MAIN(OutlineTests)
#include "outline_tests.moc"
