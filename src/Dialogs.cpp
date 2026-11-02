#include "Dialogs.h"

#include <QInputDialog>
#include <QMessageBox>
#include <QPushButton>

namespace Dialogs {

namespace {

void prepare(QMessageBox& box, const QString& title, const QString& text)
{
    box.setWindowModality(Qt::WindowModal);
    box.setText(title);
    box.setInformativeText(text);
}

} // namespace

bool confirm(QWidget* parent, const QString& title, const QString& text, const QString& okText, bool destructive)
{
    QMessageBox box(parent);
    prepare(box, title, text);
    box.setIcon(destructive ? QMessageBox::Warning : QMessageBox::Question);
    QPushButton* ok = box.addButton(okText, destructive ? QMessageBox::DestructiveRole : QMessageBox::AcceptRole);
    QPushButton* cancel = box.addButton(QStringLiteral("Отмена"), QMessageBox::RejectRole);
    box.setDefaultButton(destructive ? cancel : ok);
    box.setEscapeButton(cancel);
    box.exec();
    return box.clickedButton() == ok;
}

int choose(QWidget* parent, const QString& title, const QString& text, const QStringList& buttons)
{
    QMessageBox box(parent);
    prepare(box, title, text);
    box.setIcon(QMessageBox::Question);
    QList<QPushButton*> btns;
    for (const QString& b : buttons)
        btns.append(box.addButton(b, QMessageBox::AcceptRole));
    QPushButton* cancel = box.addButton(QStringLiteral("Отмена"), QMessageBox::RejectRole);
    if (!btns.isEmpty())
        box.setDefaultButton(btns.first());
    box.setEscapeButton(cancel);
    box.exec();
    return int(btns.indexOf(qobject_cast<QPushButton*>(box.clickedButton())));
}

void error(QWidget* parent, const QString& title, const QString& text, const QStringList& details)
{
    QMessageBox box(parent);
    prepare(box, title, text);
    box.setIcon(QMessageBox::Warning);
    if (!details.isEmpty())
        box.setDetailedText(details.join(QLatin1Char('\n')));
    box.addButton(QStringLiteral("OK"), QMessageBox::AcceptRole);
    box.exec();
}

void info(QWidget* parent, const QString& title, const QString& text)
{
    QMessageBox box(parent);
    prepare(box, title, text);
    box.setIcon(QMessageBox::Information);
    box.addButton(QStringLiteral("OK"), QMessageBox::AcceptRole);
    box.exec();
}

QString askText(QWidget* parent, const QString& title, const QString& label, const QString& initial,
                const QString& okText, bool* ok)
{
    QInputDialog dlg(parent);
    dlg.setWindowModality(Qt::WindowModal);
    dlg.setWindowTitle(title);
    dlg.setLabelText(label);
    dlg.setTextValue(initial);
    dlg.setOkButtonText(okText);
    dlg.setCancelButtonText(QStringLiteral("Отмена"));
    dlg.resize(420, dlg.sizeHint().height());
    const bool accepted = dlg.exec() == QDialog::Accepted;
    if (ok)
        *ok = accepted;
    return accepted ? dlg.textValue().trimmed() : QString();
}

} // namespace Dialogs
