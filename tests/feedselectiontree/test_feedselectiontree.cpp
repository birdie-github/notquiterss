// SPDX-License-Identifier: GPL-3.0-or-later
#include "feedselectiontree.h"
#include <QtTest>
#include <QSqlQuery>
#include <algorithm>

class FeedSelectionTreeTest : public QObject
{
  Q_OBJECT
  QSqlDatabase db;
  QTreeWidget tree;
  QTreeWidgetItem *item(int id) {
    return tree.findItems(QString::number(id), Qt::MatchExactly | Qt::MatchRecursive, 1).value(0);
  }
  QList<int> selected() {
    auto result = FeedSelectionTree::checkedFeeds(&tree);
    std::sort(result.begin(), result.end());
    return result;
  }
  void check(int id, Qt::CheckState state) {
    item(id)->setCheckState(0, state);
    FeedSelectionTree::updateChecks(&tree, item(id));
  }
private slots:
  void init() {
    db = QSqlDatabase::addDatabase("QSQLITE", "tree-test");
    db.setDatabaseName(":memory:");
    QVERIFY(db.open());
    QSqlQuery q(db);
    QVERIFY(q.exec("CREATE TABLE feeds(id INTEGER, parentId INTEGER, text TEXT, xmlUrl TEXT, image BLOB, rowToParent INTEGER)"));
    QVERIFY(q.exec("INSERT INTO feeds(id,parentId,text,xmlUrl,rowToParent) VALUES "
      "(1,0,'Folder','',0),(2,1,'A','https://a',0),(3,1,'Nested','',1),"
      "(4,3,'B','https://b',0),(5,0,'Outside','https://c',1),(6,1,'Empty','',2)"));
    tree.setColumnCount(2);
    QString error;
    QVERIFY(FeedSelectionTree::populate(&tree, db, true, "All feeds", -1, error));
  }
  void cleanup() {
    tree.clear();
    db.close();
    db = QSqlDatabase();
    QSqlDatabase::removeDatabase("tree-test");
  }
  void explicitChecksOnly() {
    QVERIFY(selected().isEmpty());
    tree.setCurrentItem(item(1));
    QVERIFY(selected().isEmpty());
    check(1, Qt::Checked);
    QCOMPARE(selected(), QList<int>({2, 4}));
    QCOMPARE(item(1)->checkState(0), Qt::Checked);
    QCOMPARE(item(0)->checkState(0), Qt::PartiallyChecked);
    check(2, Qt::Unchecked);
    QCOMPARE(selected(), QList<int>({4}));
    QCOMPARE(item(1)->checkState(0), Qt::PartiallyChecked);
    check(5, Qt::Checked);
    QCOMPARE(selected(), QList<int>({4, 5}));
    check(0, Qt::Checked);
    QCOMPARE(selected(), QList<int>({2, 4, 5}));
    QCOMPARE(item(0)->checkState(0), Qt::Checked);
    check(0, Qt::Unchecked);
    QVERIFY(selected().isEmpty());
    check(6, Qt::Checked);
    QVERIFY(selected().isEmpty());
    QCOMPARE(item(6)->checkState(0), Qt::Unchecked);
  }
  void collapsedFoldersAndRepeatPopulation() {
    tree.collapseAll();
    check(1, Qt::Checked);
    QCOMPARE(selected(), QList<int>({2, 4}));
    QString error;
    QVERIFY(FeedSelectionTree::populate(&tree, db, true, "All feeds", -1, error));
    QVERIFY(selected().isEmpty());
    QCOMPARE(tree.topLevelItemCount(), 1);
  }
  void filterPreselectionPreserved() {
    QString error;
    QVERIFY(FeedSelectionTree::populate(&tree, db, true, "All Feeds", 2, error));
    QCOMPARE(selected(), QList<int>({2}));
    QVERIFY(FeedSelectionTree::populate(&tree, db, true, "All Feeds", 1, error));
    // Existing Filter Rules checks the folder and its immediate children.
    QCOMPARE(item(1)->checkState(0), Qt::Checked);
    QCOMPARE(item(2)->checkState(0), Qt::Checked);
    QCOMPARE(item(3)->checkState(0), Qt::Checked);
    QCOMPARE(item(4)->checkState(0), Qt::Unchecked);
  }
};
QTEST_MAIN(FeedSelectionTreeTest)
#include "test_feedselectiontree.moc"
