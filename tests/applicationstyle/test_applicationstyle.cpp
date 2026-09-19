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
    write(dir.filePath("metadata.qss"), "/* ApplicationStyle\nMode=typo\n*/\nQWidget {} ");
    write(dir.filePath("incomplete.qss"), "/* ApplicationStyle\nMode=fixed\nPalette.Window=#ffffff\n*/\nQWidget {} ");
    QVERIFY(ApplicationStyles::discover(dir.path()).isEmpty());
    QVERIFY(ApplicationStyles::discover(dir.filePath("missing")).isEmpty());
  }
  void leavesQssSyntaxToQt()
  {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    write(dir.filePath("brace.qss"), "QWidget { color: red;");
    QCOMPARE(ApplicationStyles::discover(dir.path()).size(), 1);
  }
  void embeddedFallbacksAndOverrides()
  {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    auto styles = ApplicationStyles::available(dir.filePath("missing"));
    QCOMPARE(styles.size(), 3);
    QCOMPARE(styles.at(0).id, QString("system"));
    QCOMPARE(styles.at(1).id, QString("light"));
    QCOMPARE(styles.at(2).id, QString("dark"));
    for (const ApplicationStyle &style : styles) {
      QVERIFY(style.builtIn);
      QVERIFY(!style.sheet.isEmpty());
    }
    const QString embeddedDark = styles.at(2).sheet;
    write(dir.filePath("renamed.qss"),
          "/* ApplicationStyle\nName=Edited Dark\nId=dark\nMode=system\n*/\nQWidget { color: red; }");
    styles = ApplicationStyles::available(dir.path());
    QCOMPARE(styles.size(), 3);
    QCOMPARE(styles.at(2).name, QString("Edited Dark"));
    QVERIFY(styles.at(2).builtIn);
    QCOMPARE(styles.at(2).fileName, dir.filePath("renamed.qss"));
    QVERIFY(QFile::remove(dir.filePath("renamed.qss")));
    styles = ApplicationStyles::available(dir.path());
    QCOMPARE(styles.size(), 3);
    QCOMPARE(styles.at(2).sheet, embeddedDark);
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
