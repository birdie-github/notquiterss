// SPDX-License-Identifier: GPL-3.0-or-later
#include "networksettingspage.h"

#include "app/globals.h"
#include "preferences/optionspagebuilder.h"
#include "preferences/settings.h"
#include "widgets/lineedit.h"

#include <QCheckBox>
#include <QComboBox>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>

NetworkSettingsPage::NetworkSettingsPage(QWidget *parent,
                                         QVector<OptionSearchEntry> *searchEntries,
                                         int pageIndex)
  : QFrame(parent)
{
  setFrameStyle(QFrame::StyledPanel | QFrame::Plain);
  auto *layout = new QVBoxLayout(this);
  OptionsPageBuilder b(this, layout, pageIndex, -1, searchEntries);

  b.radio(directConnectionButton_, optTr("Direct connection to the Internet"),
          QStringLiteral("proxy network"));
  b.radio(systemProxyButton_, optTr("System proxy configuration (if available)"),
          QStringLiteral("proxy network"));
  b.radio(manualProxyButton_, optTr("Manual proxy configuration:"),
          QStringLiteral("HTTP SOCKS5 host port username password"));

  QWidget *manualWidget = b.indented(QMargins(0, 0, 0, 0), [this](OptionsPageBuilder &sub) {
    sub.row([this](OptionsRowBuilder &r) {
      r.combo(typeProxy_, {QStringLiteral("HTTP"), QStringLiteral("SOCKS5")});
      r.label(optTr("Proxy server:"));
      r.lineEdit(editHost_);
      r.label(optTr("Port:"));
      r.lineEdit(editPort_);
      editPort_->setFixedWidth(60);
    }, QMargins(0, 0, 0, 0), false);
    sub.row([this](OptionsRowBuilder &r) {
      r.label(optTr("Username:"));
      r.lineEdit(editUser_);
      r.label(optTr("Password:"));
      r.lineEdit(editPassword_);
      editPassword_->setEchoMode(QLineEdit::Password);
    }, QMargins(0, 0, 0, 0), false);
    sub.stretch();
  });
  OptionsPageBuilder::bindEnabled(manualProxyButton_, {manualWidget});
  b.spacing(20);

  b.label(optTr("Network options for feed updates (requires application restart):"));
  b.grid(QMargins(15, 0, 5, 0), 1, [this](OptionsGridBuilder &g) {
    g.addSpinRow(0, optTr("Request timeout:"), timeoutRequest_, 0, 300, QString(), QString(),
                 QStringLiteral("connection wait seconds"));
    g.addSpinRow(1, optTr("Number of requests:"), numberRequests_, 1, 10, QString(), QString(),
                 QStringLiteral("parallel concurrent connections threads"));
    g.addSpinRow(2, optTr("Number of retries:"), numberRepeats_, 1, 10, QString(), QString(),
                 QStringLiteral("retry attempts"));
  });
  b.spacing(20);

  b.check(customUserAgent_, optTr("Use custom User-Agent"), QString(),
          QStringLiteral("ua header browser fingerprint"));
  b.indented(QMargins(15, 0, 5, 0), [this](OptionsPageBuilder &sub) {
    userAgentEdit_ = new LineEdit();
    Settings userAgentSettings(QStringLiteral("Settings"));
    hadStoredCustomUserAgent_ = userAgentSettings.contains(QStringLiteral("userAgent"));
    const bool useCustomUserAgent = userAgentSettings.contains(QStringLiteral("useCustomUserAgent"))
        ? userAgentSettings.value(QStringLiteral("useCustomUserAgent"), false).toBool()
        : hadStoredCustomUserAgent_;
    userAgentEdit_->setText(hadStoredCustomUserAgent_
        ? userAgentSettings.value(QStringLiteral("userAgent")).toString()
        : QString());
    userAgentEdit_->setPlaceholderText(optTr("Enter a custom User-Agent"));
    userAgentEdit_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    userAgentEdit_->setMinimumWidth(0);
    customUserAgent_->setChecked(useCustomUserAgent);
    userAgentEdit_->setEnabled(useCustomUserAgent);
    sub.addWidget(userAgentEdit_);

    connect(customUserAgent_, &QCheckBox::toggled, this, [this](bool checked) {
      if (checked && !hadStoredCustomUserAgent_ && !userAgentWarningAccepted_) {
        QMessageBox warning(QMessageBox::Warning, optTr("Custom User-Agent"),
                            optTr("A custom User-Agent can make this application's requests more distinctive "
                                  "and may make them easier to identify or fingerprint. Only change it if "
                                  "you understand the implications."),
                            QMessageBox::NoButton, this);
        QPushButton *continueButton = warning.addButton(optTr("Continue"), QMessageBox::AcceptRole);
        warning.addButton(QMessageBox::Cancel);
        warning.setDefaultButton(QMessageBox::Cancel);
        warning.exec();
        if (warning.clickedButton() != continueButton) {
          QSignalBlocker blocker(customUserAgent_);
          customUserAgent_->setChecked(false);
          userAgentEdit_->setEnabled(false);
          return;
        }
        userAgentWarningAccepted_ = true;
      }
      userAgentEdit_->setEnabled(checked);
    });
  });
  b.stretch(1);
}

void NetworkSettingsPage::setProxy(const QNetworkProxy &proxy)
{
  networkProxy_ = proxy;
  updateProxy();
}

void NetworkSettingsPage::updateProxy()
{
  switch (networkProxy_.type()) {
  case QNetworkProxy::HttpProxy:
  case QNetworkProxy::Socks5Proxy:
    manualProxyButton_->setChecked(true);
    if (networkProxy_.type() == QNetworkProxy::Socks5Proxy)
      typeProxy_->setCurrentIndex(1);
    break;
  case QNetworkProxy::NoProxy:
    directConnectionButton_->setChecked(true);
    break;
  case QNetworkProxy::DefaultProxy:
  default:
    systemProxyButton_->setChecked(true);
  }
  editHost_->setText(networkProxy_.hostName());
  editPort_->setText(QVariant(networkProxy_.port()).toString());
  editUser_->setText(networkProxy_.user());
  editPassword_->setText(networkProxy_.password());
}

void NetworkSettingsPage::applyProxy()
{
  if (systemProxyButton_->isChecked()) {
    networkProxy_.setType(QNetworkProxy::DefaultProxy);
  } else if (manualProxyButton_->isChecked()) {
    networkProxy_.setType(typeProxy_->currentIndex() == 1
                           ? QNetworkProxy::Socks5Proxy
                           : QNetworkProxy::HttpProxy);
  } else {
    networkProxy_.setType(QNetworkProxy::NoProxy);
  }
  networkProxy_.setHostName(editHost_->text());
  networkProxy_.setPort(editPort_->text().toInt());
  networkProxy_.setUser(editUser_->text());
  networkProxy_.setPassword(editPassword_->text());
}

bool NetworkSettingsPage::validate()
{
  if (customUserAgent_->isChecked() && userAgentEdit_->text().trimmed().isEmpty()) {
    QMessageBox::warning(this, optTr("Custom User-Agent"),
                         optTr("Enter a custom User-Agent or disable the custom User-Agent option."));
    return false;
  }
  return true;
}

void NetworkSettingsPage::save()
{
  Settings userAgentSettings(QStringLiteral("Settings"));
  const bool useCustomUserAgent = customUserAgent_->isChecked();
  const QString customUserAgent = userAgentEdit_->text().trimmed();
  userAgentSettings.setValue(QStringLiteral("useCustomUserAgent"), useCustomUserAgent);
  if (useCustomUserAgent || hadStoredCustomUserAgent_ || userAgentWarningAccepted_) {
    userAgentSettings.setValue(QStringLiteral("userAgent"), customUserAgent);
  }
  globals.setUserAgent(useCustomUserAgent, customUserAgent);

  applyProxy();
}
