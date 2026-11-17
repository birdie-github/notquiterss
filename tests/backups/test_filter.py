#!/usr/bin/env python3
"""Exercise the production filtering SQL without compiling or launching Qt."""
import ast
from pathlib import Path
import re
import sqlite3
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SOURCE = (ROOT / 'src/backup/databasebackup.cpp').read_text()
START = SOURCE.index('bool filterCopy(sqlite3 *db, QString &error)')
END = SOURCE.index('\n}\n', START)
BLOCK = SOURCE[START:END]
assert BLOCK.count('return execute(db,') == 1
assert BLOCK.count(', error);') == 1
# Only decode the adjacent string literals of this bounded SQL expression.
SQL = ''.join(ast.literal_eval(s) for s in re.findall(r'"(?:[^"\\]|\\.)*"', BLOCK))


def fixture():
    db = sqlite3.connect(':memory:')
    db.executescript('''
        CREATE TABLE feeds(id INTEGER PRIMARY KEY, parentId INTEGER, xmlUrl TEXT,
          unread INTEGER, newCount INTEGER, undeleteCount INTEGER, currentNews INTEGER);
        CREATE TABLE news(id INTEGER PRIMARY KEY, feedId INTEGER, starred INTEGER,
          label TEXT, read INTEGER, new INTEGER, deleted INTEGER, content TEXT);
        CREATE TABLE news_ex(id INTEGER PRIMARY KEY, newsId INTEGER, value TEXT);
        CREATE TABLE labels(id INTEGER PRIMARY KEY, currentNews INTEGER);
        CREATE TABLE filters(id INTEGER PRIMARY KEY, name TEXT);
        INSERT INTO feeds VALUES(1,0,'',999,999,999,1),(2,1,'',999,999,999,2),
          (3,2,'https://example.org/feed',999,999,999,3),(4,1,'https://example.org/empty',999,999,999,4);
        INSERT INTO news VALUES
          (1,3,0,NULL,0,1,0,'EXCLUDED_UNREAD_PAYLOAD'),
          (2,3,0,'',1,0,0,'EXCLUDED_READ_PAYLOAD'),
          (3,3,1,NULL,0,1,0,'STARRED_UNREAD'),
          (4,3,1,'',1,0,0,'STARRED_READ'),
          (5,3,0,',1,',0,1,0,'LABELLED_UNREAD'),
          (6,3,0,',1,',1,0,0,'LABELLED_READ'),
          (7,3,1,',1,',1,0,0,'BOTH'),
          (8,3,0,',',0,0,0,'EMPTY_LABEL'),
          (9,3,1,NULL,0,0,1,'DELETED_STARRED'),
          (10,3,NULL,' , ',0,0,0,'EMPTY_LABEL_WHITESPACE');
        INSERT INTO news_ex VALUES(1,1,'excluded'),(2,3,'retained'),(3,999,'orphan'),(4,NULL,'orphan');
        INSERT INTO labels VALUES(1,1),(2,3);
        INSERT INTO filters VALUES(1,'preserve this filter');
    ''')
    return db


class CleanBackupTest(unittest.TestCase):
    def test_snapshot_filters_only_copy_and_repairs_counts(self):
        source = fixture()
        before = list(source.iterdump())
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'feeds.db.backup'
            copy = sqlite3.connect(path)
            source.backup(copy)
            copy.executescript(SQL)
            self.assertEqual(copy.execute('PRAGMA integrity_check').fetchone(), ('ok',))
            self.assertEqual([r[0] for r in copy.execute('SELECT id FROM news ORDER BY id')], [3,4,5,6,7,9])
            self.assertEqual(copy.execute('SELECT id FROM news_ex').fetchall(), [(2,)])
            for feed in (1,2,3):
                self.assertEqual(copy.execute('SELECT unread,newCount,undeleteCount FROM feeds WHERE id=?', (feed,)).fetchone(), (2,2,5))
            self.assertEqual(copy.execute('SELECT unread,newCount,undeleteCount FROM feeds WHERE id=4').fetchone(), (0,0,0))
            self.assertEqual(copy.execute('SELECT currentNews FROM feeds ORDER BY id').fetchall(), [(0,),(0,),(3,),(4,)])
            self.assertEqual(copy.execute('SELECT currentNews FROM labels ORDER BY id').fetchall(), [(0,),(3,)])
            self.assertEqual(copy.execute('SELECT name FROM filters').fetchall(), [('preserve this filter',)])
            copy.close()
            self.assertNotIn(b'EXCLUDED_UNREAD_PAYLOAD', path.read_bytes())
            self.assertNotIn(b'EXCLUDED_READ_PAYLOAD', path.read_bytes())
        self.assertEqual(list(source.iterdump()), before)
        source.close()

    def test_empty_result_and_cyclic_folders_terminate(self):
        db = fixture()
        db.execute('UPDATE feeds SET parentId=2 WHERE id=1')
        db.execute('UPDATE news SET starred=0,label=NULL')
        db.commit()
        db.executescript(SQL)
        self.assertEqual(db.execute('SELECT count(*) FROM news').fetchone(), (0,))
        self.assertEqual(db.execute('SELECT count(*) FROM news_ex').fetchone(), (0,))
        self.assertEqual(db.execute('SELECT sum(unread),sum(newCount),sum(undeleteCount) FROM feeds').fetchone(), (0,0,0))
        db.close()


if __name__ == '__main__':
    unittest.main()
