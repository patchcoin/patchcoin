#ifndef GRADIENTTOOLBUTTON_H
#define GRADIENTTOOLBUTTON_H

#include <QToolButton>
#include <QPainter>

class GradientToolButton : public QToolButton
{
    Q_OBJECT

public:
    explicit GradientToolButton(QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *event) override;
};

#endif // GRADIENTTOOLBUTTON_H
