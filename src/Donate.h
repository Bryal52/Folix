#pragma once

#include <QToolButton>

// Coffee-cup button between the panels; opens the window with the author's card details.
class DonateButton : public QToolButton {
    Q_OBJECT
public:
    explicit DonateButton(QWidget* parent = nullptr);

};

namespace Donate {
void showWindow(QWidget* parent);
}
