#include <QtTest>
#include <QFile>
#include <QTemporaryDir>
#include "applicationstyle.h"

class ApplicationStyleTest : public QObject
{
  Q_OBJECT
  static void write(const QString &path, const QByteArray &bytes)
  {
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(bytes), qint64(bytes.size()));
  }
private slots:
  void discoveryAndRefresh()
  {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    write(dir.filePath("first.qss"), "QWidget { color: red; }");
    write(dir.filePath("article.css"), "not an application stylesheet");
    auto styles = ApplicationStyles::discover(dir.path());
    QCOMPARE(styles.size(), 1);
    QCOMPARE(styles.first().id, QString("first"));
    write(dir.filePath("second.qss"), "/* ApplicationStyle\nName=Custom\nId=stable\nMode=system\nDefault=true\n*/\nQWidget { color: white; }");
    styles = ApplicationStyles::discover(dir.path());
    QCOMPARE(styles.size(), 2);
    QCOMPARE(styles.last().name, QString("Custom"));
    QCOMPARE(styles.last().id, QString("stable"));
    QVERIFY(styles.last().followsSystem());
    QVERIFY(styles.last().isDefault);
    QVERIFY(QFile::remove(dir.filePath("first.qss")));
    QCOMPARE(ApplicationStyles::discover(dir.path()).size(), 1);
  }
  void rejectsInvalidFiles()
  {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    write(dir.filePath("empty.qss"), "");
    write(dir.filePath("encoding.qss"), QByteArray::fromHex("fffe"));
    write(dir.filePath("brace.qss"), "QWidget { color: red;");
    write(dir.filePath("comment.qss"), "/* unfinished");
    write(dir.filePath("quote.qss"), "QWidget { image: url(\"unfinished); }");
    write(dir.filePath("metadata.qss"), "/* ApplicationStyle\nMode=typo\n*/\nQWidget {} ");
    write(dir.filePath("incomplete.qss"), "/* ApplicationStyle\nMode=fixed\nPalette.Window=#ffffff\n*/\nQWidget {} ");
    QVERIFY(ApplicationStyles::discover(dir.path()).isEmpty());
    QVERIFY(ApplicationStyles::discover(dir.filePath("missing")).isEmpty());
  }
  void handlesCommentsAndDuplicates()
  {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QByteArray sheet("/* ApplicationStyle\nId=same\n*/\nQWidget { /* } */ image: url(\"{.png\"); }");
    write(dir.filePath("a.qss"), sheet);
    write(dir.filePath("b.qss"), sheet);
    QCOMPARE(ApplicationStyles::discover(dir.path()).size(), 1);
  }
};
QTEST_GUILESS_MAIN(ApplicationStyleTest)
#include "test_applicationstyle.moc"
