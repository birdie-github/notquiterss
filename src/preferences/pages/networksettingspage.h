// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef NETWORKSETTINGSPAGE_H
#define NETWORKSETTINGSPAGE_H

#include <QFrame>
#include <QNetworkProxy>
#include <QVector>

class LineEdit;
class QCheckBox;
class QComboBox;
class QRadioButton;
class QSpinBox;
struct OptionSearchEntry;

/**
 * Network, proxy, and User-Agent settings page in OptionsDialog.
 */
class NetworkSettingsPage : public QFrame
{
  Q_OBJECT
public:
  explicit NetworkSettingsPage(QWidget *parent = nullptr,
                               QVector<OptionSearchEntry> *searchEntries = nullptr,
                               int pageIndex = -1);

  QNetworkProxy proxy() const { return networkProxy_; }
  void setProxy(const QNetworkProxy &proxy);
  bool validate();
  void save();

  QSpinBox *timeoutRequest_ = nullptr;
  QSpinBox *numberRequests_ = nullptr;
  QSpinBox *numberRepeats_ = nullptr;
  QCheckBox *customUserAgent_ = nullptr;
  LineEdit *userAgentEdit_ = nullptr;

private:
  void updateProxy();
  void applyProxy();

  QRadioButton *systemProxyButton_ = nullptr;
  QRadioButton *directConnectionButton_ = nullptr;
  QRadioButton *manualProxyButton_ = nullptr;
  QComboBox *typeProxy_ = nullptr;
  LineEdit *editHost_ = nullptr;
  LineEdit *editPort_ = nullptr;
  LineEdit *editUser_ = nullptr;
  LineEdit *editPassword_ = nullptr;
  QNetworkProxy networkProxy_;
  bool hadStoredCustomUserAgent_ = false;
  bool userAgentWarningAccepted_ = false;
};

#endif // NETWORKSETTINGSPAGE_H
