// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef OPTIONSSEARCHPANEL_H
#define OPTIONSSEARCHPANEL_H

#include "preferences/optionspagebuilder.h"

#include <QPointer>
#include <QString>
#include <QVector>
#include <QWidget>
#include <functional>

class QLabel;
class QLineEdit;
class QScrollArea;
class QTreeWidget;
class QTreeWidgetItem;

/**
 * Virtual "Search Results" page and target highlight overlay for OptionsDialog.
 */
class OptionsSearchPanel : public QWidget
{
  Q_OBJECT
public:
  using SectionLookup = std::function<QString(const OptionSearchEntry &)>;

  explicit OptionsSearchPanel(QWidget *parent = nullptr);

  QTreeWidget *resultsTree() const { return resultsTree_; }

  void updateResults(const QString &query,
                     const QVector<OptionSearchEntry> &entries,
                     const SectionLookup &sectionForEntry);
  void clear();
  bool hasResults() const;
  int currentEntryIndex() const;
  void focusFirstResult();
  void highlightWidget(QWidget *target, QScrollArea *scrollArea);

signals:
  void entryActivated(int entryIndex);

private:
  QTreeWidget *resultsTree_ = nullptr;
  QLabel *emptyLabel_ = nullptr;
  QPointer<QWidget> highlightOverlay_;
};

#endif // OPTIONSSEARCHPANEL_H
