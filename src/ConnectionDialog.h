#pragma once

#include "FtpBackend.h"

#include <QDialog>

class QComboBox;
class QLabel;
class QLineEdit;
class QSpinBox;

class ConnectionDialog : public QDialog {
    Q_OBJECT
public:
    explicit ConnectionDialog(QWidget* parent, const FtpProfile& profile = {});
    FtpProfile profile() const;

private:
    void kindChanged();
    void test();
    void accept() override;

    FtpProfile m_original;
    QComboBox* m_kind;
    QLineEdit* m_name;
    QLineEdit* m_host;
    QSpinBox* m_port;
    QLineEdit* m_user;
    QLineEdit* m_password;
    QLabel* m_hint;
    bool m_nameTouched = false;
};
