// SPDX-License-Identifier: GPL-3.0-or-later
#include "feedssettingspage.h"

#include "app/mainapplication.h"
#include "preferences/optionspagebuilder.h"
#include "projectmetadata.h"
#include "widgets/lineedit.h"

#include <QCalendarWidget>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDate>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QToolButton>
#include <utility>

namespace {
QString optTr(const char *text, const char *disambiguation = nullptr, int n = -1)
{
  return QCoreApplication::translate("OptionsDialog", text, disambiguation, n);
}
} // namespace

FeedsSettingsPage::FeedsSettingsPage(QSqlDatabase db,
                                     std::function<bool()> isPageActive,
                                     QWidget *parent,
                                     QVector<OptionSearchEntry> *searchEntries,
                                     int pageIndex)
  : QTabWidget(parent)
  , db_(std::move(db))
  , isPageActive_(std::move(isPageActive))
{
  OptionsPageBuilder::populateTabs(this, pageIndex, searchEntries, {
    {optTr("General"), [this](OptionsPageBuilder &b) {
      b.check(updateFeedsStartUp_, optTr("Automatically update feeds on startup"),
              optTr("Refresh enabled feeds immediately when the application starts. "
                    "Otherwise, overdue scheduled feeds are checked after one minute."),
              QStringLiteral("refresh sync fetch launch"));
      b.row([this](OptionsRowBuilder &r) {
        r.check(updateFeedsEnable_, optTr("Automatically update feeds every"),
                optTr("Time spent with the application closed counts toward the interval. "
                      "Overdue feeds are refreshed after a one-minute startup grace period."),
                QStringLiteral("refresh sync fetch interval timer schedule"));
        r.spin(updateFeedsInterval_, 1, 9999);
        r.combo(updateIntervalType_, {optTr("seconds"), optTr("minutes"), optTr("hours")});
        OptionsPageBuilder::bindEnabled(updateFeedsEnable_, {updateFeedsInterval_, updateIntervalType_});
      }, QMargins(0, 0, 0, 0));
      b.spacing(10);

      b.label(optTr("Action when opening a feed:"));
      b.grid(QMargins(15, 0, 0, 10), 1, [this](OptionsGridBuilder &g) {
        g.radio(0, 0, positionLastNews_, optTr("Focus the last opened article"), QStringLiteral("open feed select"));
        g.check(0, 1, openNewsWebViewOn_, optTr("Open the article"), QString(), QStringLiteral("open feed"));
        g.radio(1, 0, positionFirstNews_, optTr("Focus the first article in the list"), QStringLiteral("open feed select"));
        g.radio(2, 0, positionUnreadNews_, optTr("Focus the first unread article"), QStringLiteral("open feed select"));
        g.radio(3, 0, nottoOpenNews_, optTr("Do nothing"), QStringLiteral("open feed"));
        connect(nottoOpenNews_, &QAbstractButton::toggled, openNewsWebViewOn_, &QWidget::setDisabled);
      });

      b.check(markIdenticalNewsRead_, optTr("Automatically mark identical news as read"), QString(),
              QStringLiteral("duplicate dedup crosspost"));
      b.spacing(15);

      b.group(avoidedOldNewsDateOn_, optTr("Do not add articles published before this date to the database:"),
              true, [this](OptionsPageBuilder &grp) {
        avoidedOldNewsDate_ = new QCalendarWidget();
        avoidedOldNewsDate_->setSelectedDate(QDate::currentDate());
        avoidedOldNewsDate_->setVerticalHeaderFormat(QCalendarWidget::NoVerticalHeader);
        avoidedOldNewsDate_->setHorizontalHeaderFormat(QCalendarWidget::SingleLetterDayNames);
        grp.addWidget(avoidedOldNewsDate_);
        grp.stretch();
      }, Qt::Horizontal, QMargins(5, 5, 5, 5), QStringLiteral("date cutoff filter ignore old"));
      avoidedOldNewsDateOn_->setChecked(false);
      b.stretch();
    }},
    {optTr("Display"), [this](OptionsPageBuilder &b) {
      b.check(alternatingRowColorsNews_, optTr("Alternating row background colors"), QString(),
              QStringLiteral("zebra striping"));
      b.spacing(10);
      b.check(simplifiedDateTime_, optTr("Simplified date and time format"), QString(),
              QStringLiteral("today yesterday relative"));

      b.row([this](OptionsRowBuilder &r) {
        const QString dateLabel = optTr("Date display format:");
        const QString timeLabel = optTr("time:");
        r.label(dateLabel);
        r.combo(formatDate_);
        const QPair<QString, const char *> dateFormats[] = {
          {QStringLiteral("31.12.99"), "dd.MM.yy"},
          {QStringLiteral("31.12.1999"), "dd.MM.yyyy"},
          {QStringLiteral("31. %1. 1999").arg(optTr("Dec")), "dd. MMM. yyyy"},
          {QStringLiteral("31. %1 1999").arg(optTr("December")), "dd. MMMM yyyy"},
          {QStringLiteral("99-12-31"), "yy-MM-dd"},
          {QStringLiteral("1999-12-31"), "yyyy-MM-dd"},
          {QStringLiteral("12/31/1999"), "MM/dd/yyyy"},
        };
        for (const auto &entry : dateFormats) {
          formatDate_->addItem(entry.first, QLatin1String(entry.second));
        }
        r.registerSearch(formatDate_, dateLabel, QString(), QStringLiteral("timestamp"));

        r.spacing(10);
        r.label(timeLabel);
        r.combo(formatTime_);
        const QPair<const char *, const char *> timeFormats[] = {
          {"13:37", "hh:mm"},
          {"13:37:09", "hh:mm:ss"},
          {"01:37 PM", "hh:mm AP"},
          {"01:37:09 PM", "hh:mm:ss AP"},
        };
        for (const auto &entry : timeFormats) {
          formatTime_->addItem(QLatin1String(entry.first), QLatin1String(entry.second));
        }
        r.registerSearch(formatTime_, dateLabel + QLatin1Char(' ') + timeLabel, QString(),
                         QStringLiteral("clock hour timestamp"));
      }, QMargins(0, 0, 0, 0));
      b.spacing(10);

      b.comboRow(mainNewsFilter_, optTr("Default article filter:"), {},
                 QStringLiteral("unread starred"));
      const QPair<QString, const char *> filterItems[] = {
        {optTr("Show All"), "filterNewsAll_"},
        {optTr("Show New"), "filterNewsNew_"},
        {optTr("Show Unread"), "filterNewsUnread_"},
        {optTr("Show Starred"), "filterNewsStar_"},
        {optTr("Show Not Starred"), "filterNewsNotStarred_"},
        {optTr("Show Unread or Starred"), "filterNewsUnreadStar_"},
        {optTr("Show Last Day"), "filterNewsLastDay_"},
        {optTr("Show Last 7 Days"), "filterNewsLastWeek_"},
      };
      for (const auto &entry : filterItems) {
        mainNewsFilter_->addItem(entry.first, QLatin1String(entry.second));
      }
      b.spacing(10);

      const QString styleLabel = optTr("Article style sheet:");
      b.label(styleLabel);
      b.grid(QMargins(15, 0, 5, 10), -1, [this](OptionsGridBuilder &g) {
        styleSheetNewsEdit_ = new LineEdit();
        auto *styleSheetNewsButton = new QPushButton(optTr("Browse..."));
        connect(styleSheetNewsButton, &QAbstractButton::clicked,
                this, &FeedsSettingsPage::selectionUserStyleNews);
        g.addWidget(0, 0, styleSheetNewsEdit_);
        g.addWidget(0, 1, styleSheetNewsButton, Qt::AlignRight);
      });
      b.registerSearch(styleSheetNewsEdit_, styleLabel, QString(),
                       QStringLiteral("css theme custom style"));
      b.stretch();
    }},
    {optTr("Reading"), [this](OptionsPageBuilder &b) {
      b.group(markNewsReadOn_, optTr("Mark articles as read:"), true, [this](OptionsPageBuilder &grp) {
        grp.row([this, &grp](OptionsRowBuilder &r) {
          markCurNewsRead_ = new QRadioButton(optTr("when selected, after "), grp.container());
          r.addWidget(markCurNewsRead_);
          r.spin(markNewsReadTime_, 0, 100);
          r.label(optTr("seconds"));
          OptionsPageBuilder::bindEnabled(markCurNewsRead_, {markNewsReadTime_});
          r.registerSearch(markCurNewsRead_,
                           optTr("Mark articles as read:") + QLatin1Char(' ') + optTr("when selected, after "),
                           QString(), QStringLiteral("auto read timer delay"));
        }, QMargins(0, 0, 0, 0));
        grp.radio(markPrevNewsRead_, optTr("after switching to another article"),
                  QStringLiteral("auto read"));
      }, Qt::Vertical, std::nullopt, QStringLiteral("auto read"));
      b.check(markReadSwitchingFeed_, optTr("Mark displayed news as read when switching feeds"), QString(),
              QStringLiteral("auto read"));
      b.check(markReadClosingTab_, optTr("Mark displayed news as read when closing tab"), QString(),
              QStringLiteral("auto read"));
      b.check(markReadMinimize_, optTr("Mark displayed news as read on minimize"), QString(),
              QStringLiteral("auto read"));
      b.spacing(10);

      b.label(optTr("Prevent accidental deletion of:"));
      b.indented(QMargins(15, 0, 0, 10), [this](OptionsPageBuilder &sub) {
        sub.check(notDeleteStarred_, optTr("starred news"), QString(), QStringLiteral("protect keep"));
        sub.check(notDeleteLabeled_, optTr("labeled news"), QString(), QStringLiteral("protect keep"));
      });
      b.check(changeBehaviorActionNUN_, optTr("Change behavior of the 'Next Unread Article' action"), QString(),
              QStringLiteral("next unread navigation"));
      b.stretch();
    }},
    {optTr("Clean Up"), [this](OptionsPageBuilder &b) {
      b.group(cleanupOnShutdownBox_, optTr("Enable cleanup on shutdown"), true, [this](OptionsPageBuilder &grp) {
        grp.grid(std::nullopt, 1, [this](OptionsGridBuilder &g) {
          g.check(0, 0, dayCleanUpOn_, optTr("Maximum article age to keep (days):"), optTr(
              "Removes expired articles on shutdown and ignores expired incoming articles on later updates. "
              "The first successful fetch of a new feed is exempt. Unread, starred and labeled articles "
              "are preserved according to the options below. Articles without a usable publication date "
              "use their received date for cleanup and retain a record to prevent reappearance."),
              QStringLiteral("retention expire prune"));

          auto *maxDayRow = new QHBoxLayout();
          maxDayRow->setContentsMargins(0, 0, 0, 0);
          maxDayCleanUp_ = new QSpinBox();
          maxDayCleanUp_->setRange(0, 9999);
          OptionsPageBuilder::bindEnabled(dayCleanUpOn_, {maxDayCleanUp_});

          const QString ageWarningText = optTr(
              "Raising this limit may cause old articles you have already read or deleted "
              "to reappear as new if a feed still lists them.");
          maxDayCleanUpWarning_ = new QToolButton();
          maxDayCleanUpWarning_->setIcon(QIcon(QStringLiteral(":/images/warning")));
          maxDayCleanUpWarning_->setIconSize(QSize(16, 16));
          maxDayCleanUpWarning_->setAutoRaise(true);
          maxDayCleanUpWarning_->setCursor(Qt::PointingHandCursor);
          maxDayCleanUpWarning_->setToolTip(ageWarningText);
          maxDayCleanUpWarning_->setAccessibleName(optTr("Article age limit warning"));
          maxDayCleanUpWarning_->hide();
          connect(maxDayCleanUpWarning_, &QAbstractButton::clicked, this, [this, ageWarningText]() {
            QMessageBox::warning(this, optTr("Maximum article age to keep"), ageWarningText, QMessageBox::Ok);
          });
          maxDayRow->addWidget(maxDayCleanUp_);
          maxDayRow->addWidget(maxDayCleanUpWarning_);
          maxDayRow->addStretch();
          g.addLayout(0, 1, maxDayRow);

          g.check(1, 0, newsCleanUpOn_, optTr("Maximum number of articles to keep:"), QString(),
                  QStringLiteral("limit count cap retention"));
          maxNewsCleanUp_ = new QSpinBox();
          maxNewsCleanUp_->setRange(0, 9999);
          OptionsPageBuilder::bindEnabled(newsCleanUpOn_, {maxNewsCleanUp_});
          g.addWidget(1, 1, maxNewsCleanUp_, Qt::AlignLeft);

          g.check(2, 0, readCleanUp_, optTr("Delete read news"), QString(), QStringLiteral("purge prune"));
          g.check(3, 0, neverUnreadCleanUp_, optTr("Never delete unread news"), QString(), QStringLiteral("preserve keep"));
          g.check(4, 0, neverStarCleanUp_, optTr("Never delete starred news"), QString(), QStringLiteral("preserve keep"));
          g.check(5, 0, neverLabelCleanUp_, optTr("Never delete labeled news"), QString(), QStringLiteral("preserve keep"));
        });
        grp.spacing(10);

        grp.check(cleanUpDeleted_, optTr("Clean up 'Deleted'"), QString(), QStringLiteral("trash empty purge"));
        grp.check(optimizeDB_, optTr("Optimize database on shutdown (slower shutdown)"), QString(),
                  QStringLiteral("vacuum sqlite compact defragment"));
        grp.spacing(10);

        grp.row([this](OptionsRowBuilder &r) {
          cleanUpPreviewLabel_ = new QLabel();
          r.addWidget(cleanUpPreviewLabel_);
          cleanUpPreviewButton_ = r.button(optTr("Preview"));
          cleanUpPreviewButton_->setEnabled(false);
          connect(cleanUpPreviewButton_, &QAbstractButton::clicked,
                  this, &FeedsSettingsPage::showCleanUpPreview);
        });
        grp.stretch();
      }, Qt::Vertical, std::nullopt, QStringLiteral("prune purge expire retention exit"));
    }},
  });

  connect(cleanupOnShutdownBox_, &QGroupBox::toggled, this, &FeedsSettingsPage::updateCleanUpPreview);
  connect(cleanupOnShutdownBox_, &QGroupBox::toggled, this, &FeedsSettingsPage::updateMaxDayCleanUpWarning);
  connect(dayCleanUpOn_, &QAbstractButton::toggled, this, &FeedsSettingsPage::updateMaxDayCleanUpWarning);
  connect(maxDayCleanUp_, QOverload<int>::of(&QSpinBox::valueChanged),
          this, &FeedsSettingsPage::updateMaxDayCleanUpWarning);
  for (QAbstractButton *btn : {static_cast<QAbstractButton *>(dayCleanUpOn_),
                               static_cast<QAbstractButton *>(newsCleanUpOn_),
                               static_cast<QAbstractButton *>(readCleanUp_),
                               static_cast<QAbstractButton *>(neverUnreadCleanUp_),
                               static_cast<QAbstractButton *>(neverStarCleanUp_),
                               static_cast<QAbstractButton *>(neverLabelCleanUp_),
                               static_cast<QAbstractButton *>(cleanUpDeleted_)}) {
    connect(btn, &QAbstractButton::toggled, this, &FeedsSettingsPage::updateCleanUpPreview);
  }
  for (QSpinBox *spin : {maxDayCleanUp_, maxNewsCleanUp_}) {
    connect(spin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &FeedsSettingsPage::updateCleanUpPreview);
  }
  connect(this, &QTabWidget::currentChanged, this, [this](int index) {
    if (index == 3) updateCleanUpPreview();
  });
}

void FeedsSettingsPage::initCleanupTracking()
{
  initialMaxDayCleanUp_ = maxDayCleanUp_->value();
  updateMaxDayCleanUpWarning();
}

void FeedsSettingsPage::onPageEntered()
{
  if (currentIndex() == 3) {
    updateCleanUpPreview();
  }
}

void FeedsSettingsPage::setOpeningFeed(int action)
{
  switch (action) {
  case 1: positionFirstNews_->setChecked(true); break;
  case 2: nottoOpenNews_->setChecked(true); break;
  case 3: positionUnreadNews_->setChecked(true); break;
  default: positionLastNews_->setChecked(true);
  }
}

int FeedsSettingsPage::getOpeningFeed() const
{
  if (positionLastNews_->isChecked())     return 0;
  if (positionFirstNews_->isChecked())    return 1;
  if (nottoOpenNews_->isChecked())        return 2;
  if (positionUnreadNews_->isChecked())   return 3;
  return 0;
}

void FeedsSettingsPage::selectionUserStyleNews()
{
  const QString path = mainApp->resourcesDir() + QLatin1Char('/') + ProjectMetadata::styles();
  const QString fileName = QFileDialog::getOpenFileName(
      this, optTr("Select Style Sheet File"), path, QStringLiteral("*.css"));
  if (!fileName.isEmpty()) styleSheetNewsEdit_->setText(fileName);
}

void FeedsSettingsPage::updateMaxDayCleanUpWarning()
{
  if (!maxDayCleanUpWarning_ || initialMaxDayCleanUp_ < 0) return;
  const bool raised = cleanupOnShutdownBox_->isChecked() &&
      dayCleanUpOn_->isChecked() &&
      maxDayCleanUp_->value() > initialMaxDayCleanUp_;
  maxDayCleanUpWarning_->setVisible(raised);
}

ShutdownCleanUpConfig FeedsSettingsPage::currentCleanUpConfig() const
{
  ShutdownCleanUpConfig config;
  config.cleanupOn = cleanupOnShutdownBox_->isChecked();
  config.dayCleanUpOn = dayCleanUpOn_->isChecked();
  config.maxDayCleanUp = maxDayCleanUp_->value();
  config.newsCleanUpOn = newsCleanUpOn_->isChecked();
  config.maxNewsCleanUp = maxNewsCleanUp_->value();
  config.readCleanUp = readCleanUp_->isChecked();
  config.neverUnreadCleanUp = neverUnreadCleanUp_->isChecked();
  config.neverStarCleanUp = neverStarCleanUp_->isChecked();
  config.neverLabelCleanUp = neverLabelCleanUp_->isChecked();
  config.cleanUpDeleted = cleanUpDeleted_->isChecked();
  return config;
}

void FeedsSettingsPage::updateCleanUpPreview()
{
  if ((isPageActive_ && !isPageActive_()) || currentIndex() != 3) {
    return;
  }
  QElapsedTimer timer;
  timer.start();
  QString error;
  cleanUpPlanValid_ = ShutdownCleanUp::evaluate(db_, currentCleanUpConfig(), cleanUpPlan_, error);
  qInfo().noquote() << QStringLiteral("[perf] OptionsDialog::updateCleanUpPreview: %1 ms (cleanupOn=%2, valid=%3, total=%4)")
      .arg(timer.elapsed())
      .arg(cleanupOnShutdownBox_->isChecked())
      .arg(cleanUpPlanValid_)
      .arg(cleanUpPlan_.totalArticles());
  if (!cleanUpPlanValid_) {
    cleanUpPreviewLabel_->setText(optTr("Cannot calculate shutdown cleanup preview: %1").arg(error));
    cleanUpPreviewButton_->setEnabled(false);
    return;
  }

  const int total = cleanUpPlan_.totalArticles();
  cleanUpPreviewLabel_->setText(
      optTr("On shutdown %n article(s) will be deleted.", nullptr, total));
  cleanUpPreviewButton_->setEnabled(cleanupOnShutdownBox_->isChecked() && total > 0);
}

void FeedsSettingsPage::showCleanUpPreview()
{
  updateCleanUpPreview();
  if (!cleanUpPlanValid_ || cleanUpPlan_.totalArticles() <= 0) return;

  CleanUpPreviewDialog dialog(cleanUpPlan_, this);
  dialog.exec();
}
