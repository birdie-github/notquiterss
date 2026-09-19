#include "applicationstyle.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>

namespace {
bool paletteRole(const QString &name, QPalette::ColorRole &role)
{
  static const QHash<QString, int> roles = {
    {QStringLiteral("Window"), QPalette::Window},
    {QStringLiteral("WindowText"), QPalette::WindowText},
    {QStringLiteral("Base"), QPalette::Base},
    {QStringLiteral("AlternateBase"), QPalette::AlternateBase},
    {QStringLiteral("Text"), QPalette::Text},
    {QStringLiteral("Button"), QPalette::Button},
    {QStringLiteral("ButtonText"), QPalette::ButtonText},
    {QStringLiteral("Highlight"), QPalette::Highlight},
    {QStringLiteral("HighlightedText"), QPalette::HighlightedText},
    {QStringLiteral("Link"), QPalette::Link},
    {QStringLiteral("LinkVisited"), QPalette::LinkVisited},
    {QStringLiteral("ToolTipBase"), QPalette::ToolTipBase},
    {QStringLiteral("ToolTipText"), QPalette::ToolTipText},
    {QStringLiteral("Light"), QPalette::Light},
    {QStringLiteral("Midlight"), QPalette::Midlight},
    {QStringLiteral("Dark"), QPalette::Dark},
    {QStringLiteral("Mid"), QPalette::Mid},
    {QStringLiteral("Shadow"), QPalette::Shadow},
    {QStringLiteral("BrightText"), QPalette::BrightText}
  };
  const auto it = roles.constFind(name);
  if (it == roles.constEnd()) return false;
  role = static_cast<QPalette::ColorRole>(it.value());
  return true;
}

bool hasRequiredPalette(const ApplicationStyle &style)
{
  static const int required[] = {
    QPalette::Window, QPalette::WindowText, QPalette::Base,
    QPalette::AlternateBase, QPalette::Text, QPalette::Button,
    QPalette::ButtonText, QPalette::Highlight, QPalette::HighlightedText,
    QPalette::Link, QPalette::LinkVisited, QPalette::ToolTipBase,
    QPalette::ToolTipText
  };
  for (int role : required) {
    if (!style.paletteColors.contains(role)) return false;
  }
  return true;
}

QColor mix(const QColor &a, const QColor &b, int aPercent)
{
  const int bPercent = 100 - aPercent;
  return QColor((a.red() * aPercent + b.red() * bPercent) / 100,
                (a.green() * aPercent + b.green() * bPercent) / 100,
                (a.blue() * aPercent + b.blue() * bPercent) / 100);
}

bool readStyle(const QFileInfo &info, ApplicationStyle &style)
{
  QFile file(info.absoluteFilePath());
  if (!file.open(QIODevice::ReadOnly)) {
    qWarning() << "Application style: cannot open" << file.fileName() << file.errorString();
    return false;
  }
  QByteArray bytes = file.readAll();
  if (file.error() != QFileDevice::NoError) {
    qWarning() << "Application style: cannot read" << file.fileName() << file.errorString();
    return false;
  }
  if (bytes.startsWith("\xef\xbb\xbf")) bytes.remove(0, 3);
  style.sheet = QString::fromUtf8(bytes);
  if (style.sheet.toUtf8() != bytes) {
    qWarning() << "Application style: invalid UTF-8" << file.fileName();
    return false;
  }
  if (style.sheet.trimmed().isEmpty()) {
    qWarning() << "Application style: empty QSS" << file.fileName();
    return false;
  }
  style.id = info.completeBaseName();
  style.name = style.id;
  style.fileName = file.fileName();
  static const QRegularExpression header(
      QStringLiteral("\\A\\s*/\\* ApplicationStyle\\r?\\n(.*?)\\*/"),
      QRegularExpression::DotMatchesEverythingOption);
  const auto match = header.match(style.sheet);
  if (style.sheet.trimmed().startsWith("/* ApplicationStyle") && !match.hasMatch()) {
    qWarning() << "Application style: malformed metadata header" << file.fileName();
    return false;
  }
  if (match.hasMatch()) {
    QSet<QString> keys;
    const QStringList lines = match.captured(1).split('\n');
    for (const QString &line : lines) {
      if (line.trimmed().isEmpty()) continue;
      const int equals = line.indexOf('=');
      const QString key = line.left(equals).trimmed();
      const QString value = line.mid(equals + 1).trimmed();
      bool valid = equals > 0 && !keys.contains(key) && !value.isEmpty();
      keys.insert(key);
      if (key == "Name") style.name = value;
      else if (key == "Id") style.id = value;
      else if (key == "Mode") {
        valid = valid && (value == "system" || value == "fixed");
        style.mode = value == "fixed" ? ApplicationStyle::Fixed : ApplicationStyle::System;
      } else if (key == "Default") {
        valid = valid && (value == "true" || value == "false");
        style.isDefault = value == "true";
      } else if (key.startsWith("Palette.")) {
        QPalette::ColorRole role;
        const QColor color(value);
        valid = valid && paletteRole(key.mid(8), role) && color.isValid();
        if (valid) style.paletteColors.insert(int(role), color);
      } else valid = false;
      if (!valid) {
        qWarning() << "Application style: invalid metadata" << file.fileName() << line;
        return false;
      }
    }
  }
  if (style.mode == ApplicationStyle::Fixed && !hasRequiredPalette(style)) {
    qWarning() << "Application style: fixed style has incomplete palette" << file.fileName();
    return false;
  }
  if (style.mode == ApplicationStyle::System && !style.paletteColors.isEmpty()) {
    qWarning() << "Application style: system style must not define Palette.* colors" << file.fileName();
    return false;
  }
  return true;
}
}

QList<ApplicationStyle> ApplicationStyles::discover(const QString &directory)
{
  QList<ApplicationStyle> result;
  const QDir dir(directory);
  if (!dir.exists()) {
    qWarning() << "Application style directory is missing:" << directory;
    return result;
  }
  QSet<QString> ids;
  bool foundDefault = false;
  const auto files = dir.entryInfoList(QStringList("*.qss"), QDir::Files, QDir::Name);
  for (const QFileInfo &info : files) {
    ApplicationStyle style;
    if (!readStyle(info, style)) continue;
    if (style.id.isEmpty() || ids.contains(style.id)) {
      qWarning() << "Application style: duplicate or empty ID" << style.id << style.fileName;
      continue;
    }
    ids.insert(style.id);
    if (style.isDefault && foundDefault) {
      qWarning() << "Application style: multiple defaults; ignoring default flag in" << style.fileName;
      style.isDefault = false;
    }
    foundDefault = foundDefault || style.isDefault;
    result.append(style);
  }
  return result;
}

QList<ApplicationStyle> ApplicationStyles::available(const QString &directory)
{
  QList<ApplicationStyle> result = {automaticDefault()};
  // Embed the same source files that are shipped for editing. Their metadata
  // supplies the IDs; filenames only locate the built-in resources.
  for (const QString &path : {QStringLiteral(":/style/light"),
                              QStringLiteral(":/style/dark")}) {
    ApplicationStyle style;
    if (readStyle(QFileInfo(path), style)) {
      style.builtIn = true;
      result.append(style);
    }
  }
  for (ApplicationStyle style : discover(directory)) {
    // System is an internal resource, never an external replacement.
    if (style.id == automaticId()) continue;
    bool replaced = false;
    for (ApplicationStyle &existing : result) {
      if (existing.id == style.id) {
        style.builtIn = existing.builtIn;
        existing = style;
        replaced = true;
        break;
      }
    }
    if (!replaced) result.append(style);
  }
  return result;
}

QString ApplicationStyles::automaticId()
{
  return QStringLiteral("system");
}

ApplicationStyle ApplicationStyles::automaticDefault()
{
  ApplicationStyle style;
  style.id = automaticId();
  style.name = QStringLiteral("System");
  style.fileName = QStringLiteral(":/style/system");
  style.mode = ApplicationStyle::System;
  style.isDefault = true;
  style.builtIn = true;
  QFile file(style.fileName);
  if (file.open(QIODevice::ReadOnly)) style.sheet = QString::fromUtf8(file.readAll());
  else qWarning() << "Application style: cannot read built-in System fallback" << file.errorString();
  return style;
}

QPalette ApplicationStyles::palette(const ApplicationStyle &style, const QPalette &fallback)
{
  if (style.followsSystem()) return fallback;

  QPalette result = fallback;
  for (auto it = style.paletteColors.constBegin(); it != style.paletteColors.constEnd(); ++it)
    result.setColor(static_cast<QPalette::ColorRole>(it.key()), it.value());

  const QColor button = result.color(QPalette::Button);
  const QColor text = result.color(QPalette::Text);
  const QColor base = result.color(QPalette::Base);
  if (!style.paletteColors.contains(QPalette::Light))
    result.setColor(QPalette::Light, button.lighter(150));
  if (!style.paletteColors.contains(QPalette::Midlight))
    result.setColor(QPalette::Midlight, button.lighter(120));
  if (!style.paletteColors.contains(QPalette::Dark))
    result.setColor(QPalette::Dark, button.darker(150));
  if (!style.paletteColors.contains(QPalette::Mid))
    result.setColor(QPalette::Mid, button.darker(120));
  if (!style.paletteColors.contains(QPalette::Shadow))
    result.setColor(QPalette::Shadow, button.darker(200));
  if (!style.paletteColors.contains(QPalette::BrightText))
    result.setColor(QPalette::BrightText,
                    text.lightness() > base.lightness() ? QColor(Qt::white) : QColor(Qt::black));
#if QT_VERSION >= QT_VERSION_CHECK(5, 12, 0)
  QColor placeholder = mix(text, base, 55);
  result.setColor(QPalette::PlaceholderText, placeholder);
#endif
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
  result.setColor(QPalette::Accent, result.color(QPalette::Highlight));
#endif
  // setColor(role, color) above deliberately initializes every color group.
  // Disabled foregrounds need their own contrast, independent of the OS.
  for (QPalette::ColorRole role : {QPalette::WindowText, QPalette::Text,
                                 QPalette::ButtonText, QPalette::Link,
                                 QPalette::LinkVisited}) {
    const QPalette::ColorRole background = role == QPalette::WindowText
        ? QPalette::Window : role == QPalette::ButtonText ? QPalette::Button : QPalette::Base;
    result.setColor(QPalette::Disabled, role,
                    mix(result.color(QPalette::Active, role), result.color(background), 45));
  }
  result.setColor(QPalette::Disabled, QPalette::Highlight,
                  mix(result.color(QPalette::Highlight), base, 45));
  result.setColor(QPalette::Disabled, QPalette::HighlightedText,
                  mix(result.color(QPalette::HighlightedText),
                      result.color(QPalette::Disabled, QPalette::Highlight), 55));
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
  result.setColor(QPalette::Disabled, QPalette::Accent,
                  result.color(QPalette::Disabled, QPalette::Highlight));
#endif
  return result;
}

QString ApplicationStyles::styleSheet(const ApplicationStyle &style, const QPalette &palette)
{
  if (style.followsSystem()) return style.sheet;

  QFile file(QStringLiteral(":/style/fixedIndicators"));
  if (!file.open(QIODevice::ReadOnly)) {
    qWarning() << "Application style: cannot read shared indicators" << file.errorString();
    return style.sheet;
  }
  const QColor base = palette.color(QPalette::Active, QPalette::Base);
  const QColor text = palette.color(QPalette::Active, QPalette::Text);
  // Marks are neutral, embedded PNGs; no SVG plugin or native theme is needed.
  const QString mark = qGray(base.rgb()) < 128 ? QStringLiteral("light") : QStringLiteral("dark");
  const QString controls = QString::fromUtf8(file.readAll())
      .arg(mix(text, base, 65).name(), mark,
           mix(text, base, 30).name(),
           palette.color(QPalette::Disabled, QPalette::Text).name());
  return controls + QLatin1Char('\n') + style.sheet;
}
