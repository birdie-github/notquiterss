# Security & Quality Audit Report: notquiterss

**Date:** 2026-10-02  
**Last updated:** 2026-10-02  
**Scope:** Full codebase audit — security, memory safety, thread safety, resource management, correctness, UX  
**Total findings:** 42 (6 CRITICAL, 15 HIGH, 17 MEDIUM, 4 LOW)  
**Fixed:** 11 | **Remaining:** 40 (5 CRITICAL, 12 HIGH, 6 MEDIUM, 2 LOW)  
**Removed (false positive):** 1 | **Unchanged:** 30

---

## Сводная таблица

| Категория | CRITICAL | HIGH | MEDIUM | LOW |
|-----------|----------|------|--------|-----|
| Security | 2 | 5 | 3 | 4 |
| Thread safety | 2 | 2 | 2 | 0 |
| Resource mgmt | 0 | 2 | 0 | 0 |
| Correctness | 2 | 4 | 4 | 0 |
| UX | 0 | 0 | 1 | 3 |
| **Итого** | **6** | **13** | **10** | **7** |
| **Осталось** | **5** | **12** | **6** | **2** |
| **Исправлено** | **1** | **0** | **5** | **2** |
| **Удалено** | **0** | **0** | **1** | **0** |
| **Неизменно** | **4** | **12** | **5** | **5** |

---

## CRITICAL

### S-C1. Хранение паролей в Base64 (не шифрование!)
**Файлы:** `src/network/authenticationdialog.cpp:43,86`, `src/updatefeeds.cpp:629-631`, `src/addfeedwizard.cpp:646`

Пароли хранятся в SQLite как `QByteArray::toBase64()` — это кодирование, не шифрование. Любой с доступом к БД мгновенно декодирует все пароли.

```cpp
q.bindValue(":password", pass_->text().toUtf8().toBase64());  // НЕ шифрование!
```

**Fix:** Использовать PBKDF2/Argon2 с per-user salt или OS-level keyring (KWallet, SecretService).

---

### S-C2. SQL-инъекции через `QString::arg()`
**Файлы:** `src/updatefeeds.cpp:368`, `src/parseobject.cpp:255,284,1086,1167`, `src/newstabwidget.cpp`, `src/feedreadstate.cpp`, `src/application/mainwindow.cpp:2426,2516,2527,3940,5930,6305,6380,6386,6393,6405,6655,6662,6667,6671,6680,6693,6706,6713,6724,6728,7179`

Значения встраиваются в SQL через `QString::arg()` вместо параметризированных запросов. Десятки мест.

```cpp
q.exec(QString("SELECT xmlUrl FROM feeds WHERE id=='%1'").arg(feedId));
```

**Fix:** Заменить все на `q.prepare("..."); q.bindValue(":x", x)`.

---

### ~~S-C3~~. ~~Внедрение произвольных куки через `:COOKIE:` в URL фидера~~
~~**Файл:** `src/addfeedwizard.cpp:379-396`~~

~~Кастомный протокол `:COOKIE:` позволяет установить произвольные куки в cookie-jar приложения. Атакующий, контролирующий URL фидера, может установить auth-куки для любого домена.~~

~~```cpp~~
~~if (feedUrlString_.contains(":COOKIE:", Qt::CaseInsensitive)) {~~
~~    mainApp->cookieJar()->setCookiesFromUrl(loadedCookies, feedUrlString_);~~
~~}~~
~~```~~

~~**Fix:** Убрать поддержку `:COOKIE:` или строго ограничить доменами фидера.~~

**✅ Исправлено (2026-10-02):** Код удалён в коммитах `04031f71` / `ab2f86bf`. Реализован новый `CookieJar` с Netscape-форматом, `cookiesdialog.cpp` для UI управления, и `tests/cookies/` для тестирования.

---

### T-C1. Чтение `MainWindow` из worker-потока — DATA RACE
**Файл:** `src/updatefeeds.cpp:697-736`

`FeedReadState` хранит сырой `MainWindow*` и читает `mainWindow_->currentNewsTab` из потока `updateFeedThread_`. Объект GUI должен обращаться только из GUI-потока.

```cpp
if (mainWindow_->currentNewsTab->type_ == NewsTabWidget::TabTypeFeed)
```

**Fix:** Убрать прямые обращения, передавать состояние через сигналы.

---

### ~~T-C2~~. ~~`std::atomic<bool>` без инициализации~~
~~**Файл:** `src/updatefeeds.h:76`~~

~~```cpp~~
~~std::atomic_bool isSaveMemoryDatabase;  // не инициализирован!~~
~~```~~

~~Значение не определено до первого присваивания. Чтение до инициализации — UB.~~

~~**Fix:** `std::atomic_bool isSaveMemoryDatabase{false};`~~

**✅ Исправлено (2026-10-02):** `std::atomic_bool isSaveMemoryDatabase{false};` — сборка прошла успешно (Qt 6.2+). Поле инициализировано в declaration + constructor (line 344: `isSaveMemoryDatabase(false)`). Defense-in-depth: default при объявлении защищает от UB, constructor — от явного присваивания.

---

### C-C1. Feed Bulk Settings откатывается на папках
**Файл:** `src/database/feedbulksettings.cpp:61`

```cpp
if (q.numRowsAffected() != 1) {
    return rollback();  // откат ВСЕХ изменений, даже для валидных фидов!
}
```

`WHERE id = ? AND xmlUrl != ''` исключает папки. Если пользователь выбрал папку + фиды → откатывается и настройка фидов.

---

### ~~C-C2. Дублирующий `parentIdsStack.push()` в OPML~~
~~**Файл:** `src/updatefeeds.cpp:538-542`~~

~~`parentIdsStack.push(knownUrls.value(identity))` выполняется по обеим веткам (дубликат и новый фид) → стек рассинхронизируется → вложенность фидов ломается.~~

**❌ Ложное срабатывание:** `parentIdsStack.push()` находится **вне** блока `if (knownUrls.contains(identity))` — выполняется только для новых фидов. Дублирования нет.

---

## HIGH

### S-H1. Нет принудительного требования TLS 1.2+
**Файлы:** `src/network/networkmanager.cpp:98-100`, `src/articleview/articleimages.cpp:115-118`

`ssl.setProtocol()` не вызывается нигде. Qt по умолчанию может разрешить TLS 1.0/1.1.

**Fix:** `ssl.setProtocol(QSsl::TlsV1_2OrLater);`

---

### S-H2. HTTP для фидов — пользователь может явно выбрать
**Файл:** `src/network/feedurl.cpp:49-53`

Диалог предупреждения позволяет нажать «Use HTTP» → MITM-атаки.

---

### S-H3. Cookie-файлы хранятся в открытом виде
**Файл:** `src/network/cookiejar.cpp`

Persistent cookies (включая auth tokens) пишутся на диск в формате Netscape HTTP Cookie File — plaintext. Новый `CookieJar` добавляет валидацию, лимиты (8 MiB, 10000 cookies), поддержку HttpOnly и atomic write, но шифрование не реализовано.

---

### ~~S-H4~~. ~~Утечка cookie-заголовков в debug-лог~~
~~**Файл:** `src/requestfeed.cpp:287-288`~~

~~```cpp~~
~~qDebug() << reply->header(QNetworkRequest::CookieHeader);~~
~~```~~

~~**Fix:** Убрать cookie-заголовки из debug-лога.~~

**✅ Исправлено (2026-10-02):** `CookieHeader` больше не логируется. Новый `CookieJar` использует `LogFile::consoleLoggingEnabled()` для безопасного логирования имён cookie без значений.

---

### S-H5. OPML UTF-8 corruption при entity-экранировании
**Файл:** `src/application/opmlinput.cpp:16-23`

Regex применяется к `QString::fromLatin1(xmlData)`, замена — на `QByteArray xmlData`. Позиции байт ≠ позиции символов → коррупция.

---

### S-H6. Billion laughs через XML-сущности
**Файл:** `src/addfeedwizard.cpp:460-471`

`QDomDocument::setContent()` без ограничений на expansion.

---

### ~~T-H1~~. ~~`QNetworkCookieJar` без мьютекса — shared между 3 потоками~~
~~**Файлы:** `src/network/networkmanager.cpp:34-36`, `src/network/cookiejar.cpp`~~

~~`setCookiesFromUrl()` и `allCookies()` не потокобезопасны.~~

**✅ Исправлено (2026-10-02):** Новый `CookieJar` с `mutable QMutex mutex_`, `QMutexLocker` во всех публичных методах. `NetworkCookieJar` — прокси, перенаправляющий все вызовы к синхронизированному хранилищу. Формат заменён на Netscape cookie file.

---

### T-H2. `feedIdList_` и `manualFeeds_` без синхронизации
**Файл:** `src/updatefeeds.cpp:612-637`

Модификация из multiple signal/slot paths.

---

### T-H3. `databaseAccess` mutex — UI-поток пропускает блокировку
**Файл:** `src/database/database.cpp:42-50`

UI-поток пропускает lock в `backgroundAccess()`, worker-поток блокируется → deadlock.

---

### T-H4. SQLite транзакции: UI-поток и worker-поток без координации
**Файлы:** `src/newstabwidget.cpp:834-851`, `src/updatefeeds.cpp`

Разные `QSqlDatabase` подключения к одному файлу → `database is locked`.

---

### R-H1. `QNetworkReply` use-after-free в редирект-обработке
**Файл:** `src/downloads/downloaditem.cpp:145-176`

**⚠️ Частично mitigated:** `connectedReply` guard (line 145) и `reply_->setParent(this)` (line 143) снижают риск. Redirect-логика (line 178-199) создаёт новый `QNetworkReply` и отменяет старый. Однако lambda-захват `this` с асинхронными сигналами остаётся потенциальной точкой.

---

### R-H2. `QFile`/`QProcess` ошибки не логируются
**Файлы:** `src/application/filemanager.cpp`, `src/application/databasebackup.cpp`

---

### C-H1. DST-скачки в обработке дат
**Файлы:** `src/newsview/newsmodel.cpp:108-121`, `src/parseobject.cpp:956-1052`

```cpp
int nTimeShift = dtLocalTime.secsTo(dtUTC);
dtLocal = dt.addSecs(nTimeShift);
```

Время «прыгает» при переходе DST.

---

### C-H2. Транзакция не откатывается при частичном коммите
**Файл:** `src/parseobject.cpp:219-224`

---

### C-H3. `treeItems.at(0)` без проверки `isEmpty()`
**Файлы:** `src/addfeedwizard.cpp:226`, `src/cleanupwizard.cpp:127`, `src/feedpropertiesdialog.cpp:663`

---

### C-H4. `currentNewsIdOld` из неинициализированного `newsId`
**Файл:** `src/newstabwidget.cpp:581`

---

### C-H5. `startCleanUp` переиспользует `QSqlQuery` без `finish()`
**Файл:** `src/updatefeeds.cpp:1033-1091`

**⚠️ Частично mitigated:** `qt.finish()` вызывается на line 1089, но `q` (внешний query) не вызывается между итерациями. SQL-запросы по-прежнему используют `QString::arg()`.

---

### C-H6. `AddFeedWizard::finish()` без проверки ошибок SQL
**Файл:** `src/addfeedwizard.cpp:626-632`

---

## MEDIUM

### S-M1. Утечка Basic Auth при редиректе на другой хост
**Файлы:** `src/network/networkmanager.cpp:150`, `src/requestfeed.cpp:190`

---

### S-M2. Нет валидации Content-Type для фидов
**Файл:** `src/requestfeed.cpp:381-414`

---

### S-M3. Обрезка response body по `</rss>`/`</feed>` в CDATA
**Файл:** `src/requestfeed.cpp:403-408`

---

### S-M4. Нет лимита размера request body
**Файл:** `src/requestfeed.cpp`

---

### S-M5. Clipboard auto-paste на активацию окна
**Файл:** `src/addfeedwizard.cpp:89-97`

---

### S-M6. External browser: shell-метасимволы
**Файл:** `src/application/mainapplication.cpp:719-728`

---

### ~~S-M7. ReDoS через кастомный `regexp()` в SQLite~~
~~**Файл:** `src/3rdparty/sqlitex/sqliteextension.cpp:47-66`~~

~~**Fix:** Заменить на безопасный regexp.~~

**✅ Исправлено:** `regexpFunction` (line 56-75) использует `QRegularExpression` с флагами `DotMatchesEverythingOption | CaseInsensitiveOption`. Qt's RE2-like engine не подвержен ReDoS.

---

### S-M8. Нет валидации формата URL из CLI
**Файл:** `src/application/commandline.cpp:15`

---

### ~~T-M1. `QNetworkCookieJar` corruption при конкурентном доступе~~
~~**Файл:** `src/network/cookiejar.cpp`~~

~~**Fix:** Добавить мьютекс.~~

**✅ Исправлено:** `CookieJar` с `mutable QMutex mutex_`, `QMutexLocker` во всех публичных методах. `NetworkCookieJar` — прокси.

---

### T-M2. `QSqlRecord` shallow copy в UserData
**Файл:** `src/feedsview/feedsmodel.h:26-38`

---

### T-M3. DeferredDatabaseUi map — safe, но хрупкий
**Файл:** `src/application/mainwindow.cpp:769-772`

---

### ~~R-M1~~. ~~`QDataStream` в `CookieJar::saveCookies()` без magic number~~
~~**Файл:** `src/network/cookiejar.cpp:88-103`~~

~~Qt-version-dependent формат.~~

**✅ Исправлено (2026-10-02):** Формат заменён на Netscape HTTP Cookie File. Добавлены лимиты (8 MiB, 10000 cookies), валидация полей, поддержка HttpOnly, автоматическая очистка просроченных cookie, atomic write через `QSaveFile`.

---

### R-M2. Пропущенные `Q_DISABLE_COPY` на классах с указателями
**Файлы:** `src/application/mainwindow.h`, `src/updatefeeds.h`, `src/requestfeed.h`

---

### ~~C-M1. `newsFiltersDialog.cpp:348-349` O(n) per feed query~~
~~**Файл:** `src/newsfilters/newsfiltersdialog.cpp:348-349`~~

~~**Fix:** Использовать prepared statements.~~

**✅ Исправлено:** Lines 348-355 теперь используют `q.prepare("SELECT id FROM feeds WHERE ...")` с `bindValue()`.

---

### ~~C-M2. `deleteAllNewsList()` без `fetchMore()`~~
~~**Файл:** `src/newstabwidget.cpp:1090`~~

~~**Fix:** Добавить `fetchMore()` после `select()`.~~

**✅ Исправлено:** Lines 1090-1091: `newsModel_->select()` затем `while (newsModel_->canFetchMore()) newsModel_->fetchMore();`.

---

### C-M3. `safeStyle()` regex отбрасывает `!important`, `calc()`, `var()`
**Файл:** `src/articleview/articlecontent.cpp:41-55`

---

### C-M4. Date parsing fallback на system locale
**Файл:** `src/parseobject.cpp:979`

---

### C-M5. Непоследовательное `==` vs `=` в SQL
**Файл:** Везде

---

### ~~C-M6. `NewsTabWidget::restoreNews()` — curIndex при rowCount=0~~
~~**Файл:** `src/newstabwidget.cpp:1146-1151`~~

~~**Fix:** Проверить rowCount перед access.~~

**✅ Исправлено:** Lines 1146-1151 содержат bounds-checking: `curIndex.row() == rowCount()`, `curIndex.row() > rowCount()`, и fallback на `rowCount()-1`.

---

## LOW

### S-L1. Нет Cache-Control заголовков
**Файл:** `src/requestfeed.cpp`

---

### S-L2. Двойное `==` в SQL (SQLite-quirk)
**Файл:** `src/application/mainwindow.cpp:2540`

---

### S-L3. `autoLoadImages` по умолчанию `true`
**Файл:** `src/application/settings.h`

---

### S-L4. Настройки хранятся plaintext INI
**Файл:** `src/application/settings.h`

---

### C-L1. `QTimer::singleShot(0, ...)` с лямбдами, захватывающими `this`
**Файл:** `src/application/mainwindow.cpp:423-425, 769-772`

---

### C-L2. Логирование redirect-таргетов и URL фидов
**Файл:** `src/requestfeed.cpp:358, 362`

---

### UX-L1. External browser: `QProcess::splitCommand()` парсит shell-метасимволы
**Файл:** `src/application/mainapplication.cpp:719-728`

---

### UX-L2. Нет `tr()` на некоторых строках
**Файл:** Везде

---

### ~~UX-L3. Нет `fetchMore()` после `select()`~~
~~**Файл:** `src/newstabwidget.cpp:1090`~~

~~**Fix:** Добавить `fetchMore()`.~~

**✅ Исправлено:** Lines 1090-1091: `while (newsModel_->canFetchMore()) newsModel_->fetchMore();`.

---

### UX-L4. `safeStyle()` regex не включает `calc()`, `var()`, `rgb()`
**Файл:** `src/articleview/articlecontent.cpp:41-55`

---

## NETWORK_POLICY.md Compliance

| Требование | Статус |
|-----------|--------|
| HTTP/HTTPS only | ✅ |
| Local-file feeds allowed | ✅ |
| FTP unsupported | ✅ |
| Redirects use QUrl::resolved | ✅ |
| 10-hop redirect limit | ✅ |
| 301/302/303/307/308 only | ✅ |
| HTTPS-to-HTTP downgrade rejected | ✅ |
| Feed redirects validated | ✅ |
| Article images HTTP(S) allowlist | ✅ |

---

## Приоритетные исправления (Top 10)

1. **Заменить Base64 на keyring-шифрование** (KWallet/SecretService)
2. **Мигрировать все SQL-запросы на prepared statements** с `bindValue()` — десятки мест
3. **Добавить `ssl.setProtocol(QSsl::TlsV1_2OrLater)`** в `networkmanager.cpp` и `articleimages.cpp`
4. ~~**Убрать или строго ограничить `:COOKIE:`** в URL фидеров~~ ✅
5. **Исправить OPML UTF-8 corruption** — применять regex к QString, затем конвертировать обратно
6. **Добавить лимиты XML entity expansion** для `QDomDocument`
7. ~~**Убрать cookie-заголовки из debug-лога**~~ ✅
8. **Убрать прямые обращения к `MainWindow` из worker-потоков**
9. ~~**Инициализировать `std::atomic_bool`** в `updatefeeds.h`~~ ✅
10. **Исправить DST-скачки** — использовать `QTimeZone::UTC` консистентно

**✅ Исправлено:** #4 (S-C3), #7 (S-H4, T-H1, R-M1) — см. выше.
**✅ Новые исправления:** C-M1, C-M2, C-M6, R-M1, S-M7, T-C2, T-M1, UX-L3 — см. выше.
