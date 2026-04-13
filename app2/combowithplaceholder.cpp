#include "combowithplaceholder.h"

#include <QLineEdit>

ComboWithPlaceholder::ComboWithPlaceholder(QWidget *parent) : QComboBox(parent)
{
    setEditable(true);
}

void ComboWithPlaceholder::setPlaceholderText(const QString &text)
{
    if(lineEdit()){
        lineEdit()->setPlaceholderText(text);
    }else{
        //Fallback for non-editable mode
        QComboBox::setPlaceholderText(text);
    }
}
