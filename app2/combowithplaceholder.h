#ifndef COMBOWITHPLACEHOLDER_H
#define COMBOWITHPLACEHOLDER_H

#include <QComboBox>

class ComboWithPlaceholder : public QComboBox
{
public:
    ComboWithPlaceholder(QWidget *parent = nullptr);

    void setPlaceholderText(const QString& text);
};

#endif // COMBOWITHPLACEHOLDER_H
