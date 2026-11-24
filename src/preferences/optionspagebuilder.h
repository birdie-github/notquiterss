// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef OPTIONSPAGEBUILDER_H
#define OPTIONSPAGEBUILDER_H

#include <QMargins>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QVector>
#include <Qt>
#include <functional>
#include <initializer_list>
#include <optional>
#include <utility>

class LineEdit;
class QAbstractButton;
class QBoxLayout;
class QCheckBox;
class QComboBox;
class QFrame;
class QGridLayout;
class QGroupBox;
class QHBoxLayout;
class QLabel;
class QLayout;
class QPushButton;
class QRadioButton;
class QSpinBox;
class QTabWidget;
class QVBoxLayout;
class QWidget;

struct OptionSearchEntry {
  OptionSearchEntry() = default;
  OptionSearchEntry(int p, int tab, QString t, QString tip = {},
                    QString kw = {}, QWidget *w = nullptr,
                    int id = -1, QString key = {}, bool shortcut = false)
    : page(p)
    , subTab(tab)
    , title(std::move(t))
    , tooltip(std::move(tip))
    , keywords(std::move(kw))
    , target(w)
    , itemId(id)
    , itemKey(std::move(key))
    , isShortcut(shortcut)
  {}

  int page = -1;
  int subTab = -1;
  QString title;
  QString tooltip;
  QString keywords;
  QWidget *target = nullptr;
  int itemId = -1;
  QString itemKey;
  bool isShortcut = false;
};

class OptionsPageBuilder;

/**
 * Helper for composing a single horizontal row of option controls while
 * optionally registering controls into the dialog's search index.
 */
class OptionsRowBuilder
{
public:
  OptionsRowBuilder(QWidget *parent, QHBoxLayout *layout,
                    int page, int subTab, QVector<OptionSearchEntry> *entries);

  QLabel *label(const QString &text);
  QCheckBox *check(const QString &text, const QString &tooltip = {},
                   const QString &keywords = {});
  QCheckBox *check(QCheckBox *&out, const QString &text,
                   const QString &tooltip = {}, const QString &keywords = {});
  QSpinBox *spin(QSpinBox *&out, int min, int max,
                 const QString &suffix = {}, const QString &tooltip = {});
  QComboBox *combo(QComboBox *&out, const QStringList &items = {});
  LineEdit *lineEdit(LineEdit *&out, const QString &placeholder = {},
                     const QString &tooltip = {});
  QPushButton *button(const QString &text);
  void spacing(int px);
  void stretch(int factor = 0);
  void addWidget(QWidget *widget, int stretch = 0, Qt::Alignment alignment = {});
  void registerSearch(QWidget *target, const QString &title,
                      const QString &tooltip = {}, const QString &keywords = {});

private:
  QWidget *parent_;
  QHBoxLayout *layout_;
  int page_;
  int subTab_;
  QVector<OptionSearchEntry> *entries_;
};

/**
 * Helper for composing aligned multi-column option rows inside a QGridLayout.
 */
class OptionsGridBuilder
{
public:
  OptionsGridBuilder(QWidget *parent, QGridLayout *layout,
                     int page, int subTab, QVector<OptionSearchEntry> *entries);

  QGridLayout *raw() const { return layout_; }

  QSpinBox *addSpinRow(int row, const QString &labelText, QSpinBox *&out,
                       int min, int max, const QString &suffix = {},
                       const QString &tooltip = {},
                       const QString &keywords = {});
  void addWidget(int row, int col, QWidget *widget, Qt::Alignment alignment = {});
  void addLayout(int row, int col, QLayout *layout, Qt::Alignment alignment = {});
  QCheckBox *check(int row, int col, QCheckBox *&out, const QString &text,
                   const QString &tooltip = {}, const QString &keywords = {});
  QRadioButton *radio(int row, int col, QRadioButton *&out, const QString &text,
                      const QString &keywords = {});

private:
  QWidget *parent_;
  QGridLayout *layout_;
  int page_;
  int subTab_;
  QVector<OptionSearchEntry> *entries_;
};

/**
 * Declarative builder for OptionsDialog form pages, sub-tabs, and groups.
 * Automatically records search metadata for each constructed option control.
 */
class OptionsPageBuilder
{
public:
  using PageFn = std::function<void(OptionsPageBuilder &)>;
  using RowFn = std::function<void(OptionsRowBuilder &)>;
  using GridFn = std::function<void(OptionsGridBuilder &)>;

  OptionsPageBuilder(QWidget *container, QBoxLayout *layout,
                     int page, int subTab = -1,
                     QVector<OptionSearchEntry> *entries = nullptr);

  static QFrame *buildFrame(int page, QVector<OptionSearchEntry> *entries,
                            const PageFn &fn);
  static QWidget *buildWidget(int page, int subTab,
                              QVector<OptionSearchEntry> *entries,
                              const PageFn &fn);
  static QTabWidget *buildTabs(int page, QVector<OptionSearchEntry> *entries,
                               const QList<QPair<QString, PageFn>> &tabs);

  static void bindEnabled(QAbstractButton *toggle,
                          std::initializer_list<QWidget *> dependents);

  QWidget *container() const { return container_; }
  QBoxLayout *layout() const { return layout_; }

  QCheckBox *check(QCheckBox *&out, const QString &text,
                   const QString &tooltip = {}, const QString &keywords = {});
  QRadioButton *radio(QRadioButton *&out, const QString &text,
                      const QString &keywords = {});
  QRadioButton *radio(const QString &text, const QString &keywords = {});
  QLabel *label(const QString &text);

  QSpinBox *spinRow(QSpinBox *&out, const QString &prefix, int min, int max,
                    const QString &trailingText = {},
                    const QString &tooltip = {},
                    const QString &keywords = {});
  QComboBox *comboRow(QComboBox *&out, const QString &labelText,
                      const QStringList &items,
                      const QString &keywords = {});
  QPushButton *pathRow(LineEdit *&out, const QString &labelText,
                       const QString &buttonText,
                       const QString &keywords = {});

  QWidget *indented(const QMargins &margins, const PageFn &fn,
                    bool zeroSpacing = false);
  QGroupBox *group(QGroupBox *&out, const QString &title, bool checkable,
                   const PageFn &fn,
                   Qt::Orientation orientation = Qt::Vertical,
                   std::optional<QMargins> margins = std::nullopt,
                   const QString &keywords = {});
  QGroupBox *group(const QString &title, const PageFn &fn,
                   Qt::Orientation orientation = Qt::Vertical,
                   std::optional<QMargins> margins = std::nullopt);
  QHBoxLayout *row(const RowFn &fn,
                   std::optional<QMargins> margins = std::nullopt,
                   bool addTrailingStretch = true);
  QGridLayout *grid(std::optional<QMargins> margins, int stretchColumn,
                    const GridFn &fn);

  void spacing(int px);
  void stretch(int factor = 0);
  void addWidget(QWidget *widget);
  void addLayout(QLayout *layout);
  void registerSearch(QWidget *target, const QString &title,
                      const QString &tooltip = {},
                      const QString &keywords = {});

private:
  QWidget *container_;
  QBoxLayout *layout_;
  int page_;
  int subTab_;
  QVector<OptionSearchEntry> *entries_;
};

#endif // OPTIONSPAGEBUILDER_H
