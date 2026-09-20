// SPDX-License-Identifier: GPL-3.0-or-later
#include "feedurl.h"
#include "networkpolicy.h"
#include <QMessageBox>
#include <QRegularExpression>
#include <QPushButton>

QUrl FeedUrl::normalize(QString input)
{
  input = input.trimmed();
  if (input.startsWith("feed:", Qt::CaseInsensitive)) input.remove(0, 5);
  if (input.startsWith("//")) input.prepend("https:");
  // A host with an explicit port must not be mistaken for a URL scheme.
  static const QRegularExpression hostPort(QStringLiteral("^[^/:?#]+:[0-9]+(?:[/?#]|$)"));
  if (!input.isEmpty() && (QUrl(input).scheme().isEmpty() || hostPort.match(input).hasMatch()))
    input.prepend("https://");
  return QUrl(input);
}

QUrl FeedUrl::upgrade(QUrl url)
{
  if (url.scheme() == "http") {
    url.setScheme("https");
    if (url.port() == 80) url.setPort(-1);
  }
  return url;
}

QString FeedUrl::identity(const QUrl &url)
{
  QUrl key = upgrade(url);
  if (key.scheme() == "https" && key.port() == 443) key.setPort(-1);
  return key.toString(QUrl::FullyEncoded);
}

bool FeedUrl::confirm(QWidget *parent, QUrl &url)
{
  if (!NetworkPolicy::isRequestUrl(url)) {
    QMessageBox::warning(parent, QObject::tr("Invalid feed URL"),
                         QObject::tr("Enter an HTTP, HTTPS or local-file feed URL."));
    return false;
  }
  if (url.scheme() != "http") return true;
  QMessageBox box(QMessageBox::Warning, QObject::tr("Unencrypted feed connection"),
      QObject::tr("This feed is unprotected (uses HTTP). Your reading activity is exposed, and the content could be altered or blocked before it reaches you.\n\n%1")
          .arg(url.toDisplayString(QUrl::RemoveUserInfo)), QMessageBox::Cancel, parent);
  box.setTextFormat(Qt::PlainText);
  auto *secure = box.addButton(QObject::tr("Use HTTPS"), QMessageBox::AcceptRole);
  auto *insecure = box.addButton(QObject::tr("Use HTTP"), QMessageBox::DestructiveRole);
  box.setDefaultButton(secure);
  box.exec();
  if (box.clickedButton() == secure) { url = upgrade(url); return true; }
  return box.clickedButton() == insecure;
}

bool FeedUrl::confirmImportHttp(QWidget *parent, int count)
{
  QMessageBox box(QMessageBox::Warning, QObject::tr("Unencrypted feed connections"),
      QObject::tr("Import %1 feeds using an insecure connection (HTTP)? Your reading activity is exposed, and the content could be altered or blocked before it reaches you.")
          .arg(count), QMessageBox::Cancel, parent);
  auto *accept = box.addButton(QObject::tr("Import HTTP feeds"), QMessageBox::DestructiveRole);
  box.setDefaultButton(QMessageBox::Cancel);
  box.exec();
  return box.clickedButton() == accept;
}
