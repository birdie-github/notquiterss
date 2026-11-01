/* ============================================================
* QuiteRSS is a open-source cross-platform RSS/Atom news feeds reader
* © 2011-2020 QuiteRSS Project
* © 2026 Artem S. Tashkinov <aros@gmx.com> and ChatGPT
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <https://www.gnu.org/licenses/>.
* ============================================================ */
#include "feedxml.h"
#include "feeddiscovery.h"
#include "logfile.h"
#include "credentialscope.h"
#include "network/feedurl.h"
#include "network/networkpolicy.h"
#include "addfeedwizard.h"

#include "mainapplication.h"
#include "addfolderdialog.h"
#include "authenticationdialog.h"
#include "toolbutton.h"
#include "settings.h"

#include <QDomDocument>
#include <QRegularExpression>

extern QString kCreateNewsTableQuery;

AddFeedWizard::AddFeedWizard(QWidget *parent, int curFolderId)
  : QWizard(parent),
    curFolderId_(curFolderId)
{
  setModal(true);
  setWindowFlags (windowFlags() & ~Qt::WindowContextHelpButtonHint);
  setWindowTitle(tr("Add Feed"));
  setWizardStyle(QWizard::ModernStyle);
  setOptions(QWizard::HaveFinishButtonOnEarlyPages |
             QWizard::NoBackButtonOnStartPage);

  addPage(createUrlFeedPage());
  addPage(createNameFeedPage());

  updateFeeds_ = new UpdateFeeds(this, true);
  updateFeeds_->start();

  connect(button(QWizard::BackButton), &QAbstractButton::clicked, this, &AddFeedWizard::backButtonClicked);
  connect(button(QWizard::NextButton), &QAbstractButton::clicked, this, &AddFeedWizard::nextButtonClicked);
  connect(button(QWizard::FinishButton), &QAbstractButton::clicked,
          this, &AddFeedWizard::finishButtonClicked);
  connect(this, &QWizard::currentIdChanged, this, &AddFeedWizard::slotCurrentIdChanged, Qt::QueuedConnection);
  resize(400, 300);

  Settings settings;
  restoreGeometry(settings.value("addFeedWizard/geometry").toByteArray());
}

AddFeedWizard::~AddFeedWizard()
{
  Settings settings;
  settings.setValue("addFeedWizard/geometry", saveGeometry());

  updateFeeds_->disconnectObjects();
  delete updateFeeds_;
}

/*virtual*/ void AddFeedWizard::done(int result)
{
  if (result == QDialog::Rejected || result == FeedSkipped) {
    if (feedId_ > 0)
      deleteFeed();
  }
  // Selection and skipping return control to the feed queue without
  // finishing the current subscription. Its page may still be incomplete.
  if (result == FeedsSelected || result == FeedSkipped)
    QDialog::done(result);
  else
    QWizard::done(result);
}

void AddFeedWizard::changeEvent(QEvent *event)
{
  if ((event->type() == QEvent::ActivationChange) &&
      isActiveWindow() && (currentId() == 0) && urlFeedEdit_->isEnabled()) {
    QClipboard *clipboard_ = QApplication::clipboard();
    QString clipboardStr = clipboard_->text().left(8);
    if (clipboardStr.startsWith("feed:", Qt::CaseInsensitive) ||
        clipboardStr.contains("http://", Qt::CaseInsensitive) ||
        clipboardStr.contains("https://", Qt::CaseInsensitive) ||
        clipboardStr.contains("www.", Qt::CaseInsensitive) ||
        clipboardStr.contains("feed://", Qt::CaseInsensitive) ||
        clipboardStr.contains("file://", Qt::CaseInsensitive)) {
      urlFeedEdit_->setText(clipboard_->text());
      urlFeedEdit_->selectAll();
      urlFeedEdit_->setFocus();
    }
  }
}

QWizardPage *AddFeedWizard::createUrlFeedPage()
{
  QWizardPage *page = new QWizardPage;
  page->setTitle(tr("Create New Feed"));

  selectedPage = false;
  finishOn = false;

  urlFeedEdit_ = new LineEdit(this);
  urlFeedEdit_->setPlaceholderText("https://example.org/feed");

  titleFeedAsName_ = new QCheckBox(
        tr("Use the feed title as the display name"), this);
  titleFeedAsName_->setChecked(true);

  authentication_ = new QGroupBox(this);
  authentication_->setTitle(tr("Server requires authentication:"));
  authentication_->setCheckable(true);
  authentication_->setChecked(false);

  user_ = new LineEdit(this);
  pass_ = new LineEdit(this);
  pass_->setEchoMode(QLineEdit::Password);

  QGridLayout *authenticationLayout = new QGridLayout();
  authenticationLayout->addWidget(new QLabel(tr("Username:")), 0, 0);
  authenticationLayout->addWidget(user_, 0, 1);
  authenticationLayout->addWidget(new QLabel(tr("Password:")), 1, 0);
  authenticationLayout->addWidget(pass_, 1, 1);

  authentication_->setLayout(authenticationLayout);

  QLabel *iconWarning = new QLabel(this);
  iconWarning->setPixmap(QPixmap(":/images/warning"));
  textWarning = new QLabel(this);
  textWarning->setTextFormat(Qt::PlainText);
  textWarning->setWordWrap(true);
  QFont font = textWarning->font();
  font.setBold(true);
  textWarning->setFont(font);

  QHBoxLayout *warningLayout = new QHBoxLayout();
  warningLayout->setContentsMargins(0, 0, 0, 0);
  warningLayout->addWidget(iconWarning);
  warningLayout->addWidget(textWarning, 1);

  warningWidget_ = new QWidget(this);
  warningWidget_->setLayout(warningLayout);

  progressBar_ = new QProgressBar(this);
  progressBar_->setObjectName("progressBar_");
  progressBar_->setTextVisible(false);
  progressBar_->setFixedHeight(15);
  progressBar_->setMinimum(0);
  progressBar_->setMaximum(0);
  progressBar_->setVisible(false);

  QVBoxLayout *layout = new QVBoxLayout;
  layout->addWidget(new QLabel(tr("Feed URL or website address:")));
  layout->addWidget(urlFeedEdit_);
  layout->addWidget(titleFeedAsName_);
  layout->addSpacing(10);
  layout->addWidget(authentication_);
  layout->addStretch(1);
  layout->addWidget(warningWidget_);
  layout->addWidget(progressBar_);
  page->setLayout(layout);

  connect(urlFeedEdit_, &QLineEdit::textChanged, this, &AddFeedWizard::urlFeedEditChanged);
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
  connect(titleFeedAsName_, &QCheckBox::checkStateChanged, this, &AddFeedWizard::titleFeedAsNameStateChanged);
#else
  connect(titleFeedAsName_, &QCheckBox::stateChanged, this, &AddFeedWizard::titleFeedAsNameStateChanged);
#endif

  return page;
}

QWizardPage *AddFeedWizard::createNameFeedPage()
{
  QWizardPage *page = new QWizardPage;
  page->setTitle(tr("Create New Feed"));
  page->setFinalPage(false);

  nameFeedEdit_ = new LineEdit(this);

  foldersTree_ = new QTreeWidget(this);
  foldersTree_->setObjectName("foldersTree_");
  foldersTree_->setColumnCount(2);
  foldersTree_->setColumnHidden(1, true);
  foldersTree_->header()->hide();

  QStringList treeItem;
  treeItem << tr("Feeds") << "Id";
  foldersTree_->setHeaderLabels(treeItem);

  treeItem.clear();
  treeItem << tr("All Feeds") << "0";
  QTreeWidgetItem *treeWidgetItem = new QTreeWidgetItem(treeItem);
  treeWidgetItem->setIcon(0, QIcon(":/images/folder"));
  foldersTree_->addTopLevelItem(treeWidgetItem);
  foldersTree_->setCurrentItem(treeWidgetItem);

  QSqlQuery q;
  QQueue<int> parentIds;
  parentIds.enqueue(0);
  while (!parentIds.empty()) {
    int parentId = parentIds.dequeue();
    QString qStr = QString("SELECT text, id FROM feeds WHERE parentId='%1' AND (xmlUrl='' OR xmlUrl IS NULL)").
        arg(parentId);
    q.exec(qStr);
    while (q.next()) {
      QString folderText = q.value(0).toString();
      QString folderId = q.value(1).toString();

      QStringList treeItem;
      treeItem << folderText << folderId;
      QTreeWidgetItem *treeWidgetItem = new QTreeWidgetItem(treeItem);

      treeWidgetItem->setIcon(0, QIcon(":/images/folder"));

      QList<QTreeWidgetItem *> treeItems =
            foldersTree_->findItems(QString::number(parentId),
                                                       Qt::MatchFixedString | Qt::MatchRecursive,
                                                       1);
      treeItems.at(0)->addChild(treeWidgetItem);
      if (folderId.toInt() == curFolderId_)
        foldersTree_->setCurrentItem(treeWidgetItem);
      parentIds.enqueue(folderId.toInt());
    }
  }

  foldersTree_->expandAll();
  foldersTree_->sortByColumn(0, Qt::AscendingOrder);

  ToolButton *newFolderButton = new ToolButton(this);
  newFolderButton->setIcon(QIcon(":/images/addT"));
  newFolderButton->setToolTip(tr("New Folder..."));
  newFolderButton->setAutoRaise(true);

  QHBoxLayout *newFolderLayout = new QHBoxLayout;
  newFolderLayout->setContentsMargins(0, 0, 0, 0);
  newFolderLayout->addWidget(newFolderButton);
  newFolderLayout->addStretch();
  QVBoxLayout *newFolderVLayout = new QVBoxLayout;
  newFolderVLayout->setContentsMargins(2, 2, 2, 2);
  newFolderVLayout->addStretch();
  newFolderVLayout->addLayout(newFolderLayout);

  foldersTree_->setLayout(newFolderVLayout);

  QVBoxLayout *layout = new QVBoxLayout;
  layout->addWidget(new QLabel(tr("Displayed name:")));
  layout->addWidget(nameFeedEdit_);
  layout->addWidget(new QLabel(tr("Location:")));
  layout->addWidget(foldersTree_);
  page->setLayout(layout);

  connect(nameFeedEdit_, &QLineEdit::textChanged, this, &AddFeedWizard::nameFeedEditChanged);
  connect(newFolderButton, &QAbstractButton::clicked, this, &AddFeedWizard::newFolder);

  return page;
}

void AddFeedWizard::configureFeed(const QString &url, int position, int total)
{
  configuring_ = true;
  setUrlFeed(url);
  page(1)->setSubTitle(url.toHtmlEscaped());
  setWindowTitle(tr("Create New Feed (%1 of %2)").arg(position).arg(total));
  setOption(QWizard::HaveCustomButton1, total > 1);
  setButtonText(QWizard::CustomButton1, tr("Skip"));
  connect(button(QWizard::CustomButton1), &QAbstractButton::clicked, this, [this] { done(FeedSkipped); });
  QTimer::singleShot(0, this, &AddFeedWizard::nextButtonClicked);
}

void AddFeedWizard::setUrlFeed(const QString &feedUrl)
{
  urlFeedEdit_->setText(feedUrl);
}

void AddFeedWizard::urlFeedEditChanged(const QString& text)
{
  button(QWizard::NextButton)->setEnabled(
        NetworkPolicy::isRequestUrl(FeedUrl::normalize(text)));

  bool buttonEnable = false;
  if (titleFeedAsName_->isChecked() && NetworkPolicy::isRequestUrl(FeedUrl::normalize(text))) {
    buttonEnable = true;
  }
  warningWidget_->setVisible(false);
  button(QWizard::FinishButton)->setEnabled(buttonEnable);
}

void AddFeedWizard::titleFeedAsNameStateChanged(int state)
{
  bool buttonEnable = false;
  if ((state == Qt::Checked) && NetworkPolicy::isRequestUrl(FeedUrl::normalize(urlFeedEdit_->text()))) {
    buttonEnable = true;
  }
  button(QWizard::FinishButton)->setEnabled(buttonEnable);
}

void AddFeedWizard::nameFeedEditChanged(const QString& text)
{
  button(QWizard::FinishButton)->setEnabled(!text.isEmpty());
}

void AddFeedWizard::backButtonClicked()
{
  deleteFeed();
  nameFeedEdit_->clear();
  page(0)->setEnabled(true);
  selectedPage = false;
}

void AddFeedWizard::nextButtonClicked()
{
  if (currentId() == 0) {
    finishOn = false;
    addFeed();
  }
}

void AddFeedWizard::finishButtonClicked()
{
  if (currentId() == 0) {
    finishOn = true;
    addFeed();
  } else if (currentId() == 1) {
    finish();
  }
}

void AddFeedWizard::addFeed()
{
  if (!page(0)->isEnabled()) return;
  QUrl feedUrl = FeedUrl::normalize(urlFeedEdit_->text());
  if (!FeedUrl::confirm(this, feedUrl)) return;
  feedUrlString_ = feedUrl.toString();
  urlFeedEdit_->setText(feedUrlString_);

  if (feedUrl.host().isEmpty() && !feedUrl.isLocalFile()) {
    textWarning->setText(tr("Invalid URL"));
    warningWidget_->setVisible(true);
    return;
  }

  QSqlQuery q;
  int duplicateFoundId = -1;
  q.prepare("SELECT id FROM feeds WHERE xmlUrl = :xmlUrl");
  q.bindValue(":xmlUrl", feedUrlString_);
  q.exec();
  if (q.first())
    duplicateFoundId = q.value(0).toInt();

  if (0 <= duplicateFoundId) {
    textWarning->setText(tr("Duplicate feed"));
    warningWidget_->setVisible(true);
  } else {
    button(QWizard::NextButton)->setEnabled(false);
    button(QWizard::CancelButton)->setEnabled(false);
    button(QWizard::FinishButton)->setEnabled(false);
    page(0)->setEnabled(false);
    showProgressBar();

    const QString credentials = authentication_->isChecked()
        ? QString("%1:%2").arg(user_->text(), pass_->text()) : QString();
    emit signalRequestUrl(0, feedUrlString_, QDateTime(), credentials);
  }
}

bool AddFeedWizard::insertFeed()
{
  QSqlQuery q;
  q.prepare("INSERT INTO feeds(xmlUrl, created, rowToParent, authentication, disableUpdate) "
            "SELECT ?, ?, count(*), ?, 1 FROM feeds WHERE parentId=0 "
            "HAVING NOT EXISTS (SELECT 1 FROM feeds WHERE xmlUrl=?)");
  q.addBindValue(feedUrlString_);
  q.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
  q.addBindValue(int(authentication_->isChecked()));
  q.addBindValue(feedUrlString_);
  if (!q.exec()) { failed(q.lastError().text()); return false; }
  if (q.numRowsAffected() != 1) { failed(tr("Duplicate feed")); return false; }
  feedId_ = q.lastInsertId().toInt();
  return true;
}

void AddFeedWizard::deleteFeed()
{
  QSqlQuery q;
  q.exec(QString("DELETE FROM feeds WHERE id='%1'").arg(feedId_));
  q.exec(QString("DELETE FROM news WHERE feedId='%1'").arg(feedId_));
  q.exec(QString("DELETE FROM feeds_ex WHERE feedId='%1'").arg(feedId_));

  feedId_ = 0;
  // Correct rowToParent field
  QList<int> idList;
  q.exec("SELECT id FROM feeds WHERE parentId=0 ORDER BY rowToParent");
  while (q.next()) {
    idList << q.value(0).toInt();
  }
  for (int i = 0; i < idList.count(); i++) {
    q.exec(QString("UPDATE feeds SET rowToParent='%1' WHERE id=='%2'").
           arg(i).arg(idList.at(i)));
  }
  q.finish();
}

void AddFeedWizard::slotCurrentIdChanged(int idPage)
{
  if (idPage == 0)
    urlFeedEditChanged(urlFeedEdit_->text());
  else if (idPage == 1)
    nameFeedEditChanged(nameFeedEdit_->text());
}

/*virtual*/ bool AddFeedWizard::validateCurrentPage()
{
  if (!selectedPage) {
    return false;
  }
  return true;
}

void AddFeedWizard::showProgressBar()
{
  progressBar_->show();
  QTimer::singleShot(250, this, &AddFeedWizard::slotProgressBarUpdate);
}

void AddFeedWizard::slotProgressBarUpdate()
{
  progressBar_->update();
  if (progressBar_->isVisible())
    QTimer::singleShot(250, this, &AddFeedWizard::slotProgressBarUpdate);
}

void AddFeedWizard::failed(const QString &message)
{
  progressBar_->hide();
  page(0)->setEnabled(true);
  selectedPage = false;
  finishOn = false;
  button(QWizard::CancelButton)->setEnabled(true);
  urlFeedEditChanged(urlFeedEdit_->text());
  textWarning->setText(message);
  warningWidget_->setVisible(true);
}

void AddFeedWizard::getUrlDone(int result, int id, QString, QString error, QByteArray data,
                               QDateTime dtReply, QString codecName, QUrl effective, QString mimeType)
{
  if (id != 0) return; // Probe completions belong to the discovery dialog.
  if (result < 0 || data.isEmpty()) {
    failed(error.isEmpty() ? tr("Request failed") : error);
    return;
  }
  QDomDocument doc;
  int line, column;
  if (FeedXml::parse(doc, data, error, line, column) && FeedXml::isFeed(doc)) {
    if (!insertFeed()) return;
    if (configuring_) button(QWizard::CustomButton1)->setEnabled(false);
    emit xmlReadyParse(data, feedId_, dtReply, codecName);
    return;
  }

  const QByteArray prefix = data.left(512).trimmed().toLower();
  if (configuring_ || !NetworkPolicy::isHttpUrl(effective) ||
      !(mimeType == "text/html" || mimeType == "application/xhtml+xml" ||
        prefix.startsWith("<!doctype html") || prefix.startsWith("<html"))) {
    failed(tr("The server returned content that is not a valid RSS or Atom feed."));
    return;
  }

  FeedDiscovery discovery(data, codecName, effective, QUrl(feedUrlString_), scanId_--, this);
  auto *worker = updateFeeds_->requestFeed_;
  connect(&discovery, &FeedDiscovery::stopRequests, worker, &RequestFeed::stopRequest);
  connect(&discovery, &FeedDiscovery::requestUrl, worker, &RequestFeed::requestUrl);
  connect(worker, &RequestFeed::getUrlDone, &discovery, &FeedDiscovery::received);
  const int choice = discovery.exec();
  QMetaObject::invokeMethod(worker, &RequestFeed::stopRequest, Qt::QueuedConnection);
  if (choice == QDialog::Accepted && !mainApp->isClosing()) {
    selectedUrls_ = discovery.selection();
    done(FeedsSelected);
  } else {
    failed(QString());
    warningWidget_->hide();
  }
}

void AddFeedWizard::slotUpdateFeed(int feedId, bool, int newCount, QString)
{
  qDebug() << "ParseDone: " << LogFile::urlForLog(QUrl(feedUrlString_));
  if (configuring_) button(QWizard::CustomButton1)->setEnabled(true);
  selectedPage = true;
  newCount_ = newCount;

  if (titleFeedAsName_->isChecked()) {
    QSqlQuery q;
    q.exec(QString("SELECT title FROM feeds WHERE id=='%1'").arg(feedId));
    if (q.first()) nameFeedEdit_->setText(q.value(0).toString());
    nameFeedEdit_->selectAll();
    nameFeedEdit_->setFocus();
  }
  if (!finishOn) {
    button(QWizard::CancelButton)->setEnabled(true);
    next();
  } else {
    finish();
  }
  progressBar_->hide();
}

void AddFeedWizard::finish()
{
  QSqlQuery q;
  q.exec(QString("SELECT htmlUrl FROM feeds WHERE id=='%1'").arg(feedId_));
  if (q.first())
    htmlUrlString_ = q.value(0).toString();

  feedParentId_ = foldersTree_->currentItem()->text(1).toInt();

  // Correct rowToParent field
  QList<int> idList;
  q.exec("SELECT id FROM feeds WHERE parentId=0 ORDER BY rowToParent");
  while (q.next()) {
    if (feedId_ != q.value(0).toInt())
      idList << q.value(0).toInt();
  }
  for (int i = 0; i < idList.count(); i++) {
    q.exec(QString("UPDATE feeds SET rowToParent='%1' WHERE id=='%2'").
           arg(i).arg(idList.at(i)));
  }

  // Calculate row number to insert feed
  int rowToParent = 0;
  q.exec(QString("SELECT count(id) FROM feeds WHERE parentId='%1' AND id!='%2'").
         arg(feedParentId_).arg(feedId_));
  if (q.next()) rowToParent = q.value(0).toInt();

  int auth = 0;
  if (authentication_->isChecked()) auth = 1;

  q.prepare("UPDATE feeds SET text = ?, parentId = ?, rowToParent = ?, authentication = ? WHERE id == ?");
  q.addBindValue(nameFeedEdit_->text());
  q.addBindValue(feedParentId_);
  q.addBindValue(rowToParent);
  q.addBindValue(auth);
  q.addBindValue(feedId_);
  q.exec();

  if (auth) {
    QUrl url(feedUrlString_);
    QString server = CredentialScope::feed(url);

    q.prepare("SELECT * FROM passwords WHERE server=?");
    q.addBindValue(server);
    q.exec();
    if (!q.next()) {
      q.prepare("INSERT INTO passwords (server, username, password) "
                "VALUES (:server, :username, :password)");
      q.bindValue(":server", server);
      q.bindValue(":username", user_->text());
      q.bindValue(":password", pass_->text().toUtf8().toBase64());
      q.exec();
    }
  }

  // Publish the subscription only after its properties and credentials are saved.
  q.prepare("UPDATE feeds SET disableUpdate=0 WHERE id=?");
  q.addBindValue(feedId_);
  if (!q.exec()) {
    QMessageBox::warning(this, tr("Error"), q.lastError().text());
    return;
  }

  accept();
}

/*! \brief Adding new folder **************************************************/
void AddFeedWizard::newFolder()
{
  AddFolderDialog *addFolderDialog = new AddFolderDialog(this);
  QList<QTreeWidgetItem *> treeItems =
      addFolderDialog->foldersTree_->findItems(foldersTree_->currentItem()->text(1),
                                               Qt::MatchFixedString | Qt::MatchRecursive,
                                               1);
  addFolderDialog->foldersTree_->setCurrentItem(treeItems.at(0));

  if (addFolderDialog->exec() == QDialog::Rejected) {
    delete addFolderDialog;
    return;
  }

  int folderId = 0;
  QString folderText = addFolderDialog->nameFeedEdit_->text();
  int parentId = addFolderDialog->foldersTree_->currentItem()->text(1).toInt();

  // Calculate row number to insert folder
  int rowToParent = 0;
  QSqlQuery q;
  q.exec(QString("SELECT count(id) FROM feeds WHERE parentId='%1'").arg(parentId));
  if (q.first())
    rowToParent = q.value(0).toInt();

  // Add folder
  q.prepare("INSERT INTO feeds(text, created, parentId, rowToParent) "
            "VALUES (:text, :feedCreateTime, :parentId, :rowToParent)");
  q.bindValue(":text", folderText);
  q.bindValue(":feedCreateTime",
              QLocale::c().toString(QDateTime::currentDateTimeUtc(), "yyyy-MM-ddTHH:mm:ss"));
  q.bindValue(":parentId", parentId);
  q.bindValue(":rowToParent", rowToParent);
  q.exec();

  folderId = q.lastInsertId().toInt();
  q.finish();

  treeItems = foldersTree_->findItems(QString::number(parentId),
                                      Qt::MatchFixedString | Qt::MatchRecursive,
                                      1);
  QStringList treeItem;
  treeItem << folderText << QString::number(folderId);
  QTreeWidgetItem *treeWidgetItem = new QTreeWidgetItem(treeItem);
  treeItems.at(0)->addChild(treeWidgetItem);
  foldersTree_->setCurrentItem(treeWidgetItem);

  delete addFolderDialog;
}
