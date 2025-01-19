#include "gradienttoolbutton.h"
#include <QPainter>
#include <QStyleOptionToolButton>
#include <QHoverEvent>

GradientToolButton::GradientToolButton(QWidget *parent)
    : QToolButton(parent)
{
}

void GradientToolButton::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    bool isHovered = this->underMouse();
    bool isPressed = this->isDown();

    QColor gradientStart = QColor("#a4ffa3");
    QColor gradientMiddle = QColor("#00ff92");
    QColor gradientEnd = QColor("#00db98");

    if (isPressed) {
        gradientStart = QColor("#00b87a");
        gradientMiddle = QColor("#009f63");
        gradientEnd = QColor("#008751");
    } else if (isHovered) {
        gradientStart = QColor("#c4ffc3");
        gradientMiddle = QColor("#a0ffa0");
        gradientEnd = QColor("#80d880");
    }

    QRect adjustedRect = rect().adjusted(6, 6, -5, -5);

    QLinearGradient gradient(adjustedRect.topLeft(), adjustedRect.bottomLeft());
    gradient.setColorAt(0.0, gradientStart);
    gradient.setColorAt(0.5, gradientMiddle);
    gradient.setColorAt(1.0, gradientEnd);

    if (isHovered || isPressed) {
        painter.setBrush(gradient);
    } else {
        painter.setBrush(Qt::NoBrush);
    }

    painter.setPen(Qt::NoPen);
    painter.drawRoundedRect(adjustedRect, 15, 15);

    QPen pen(QBrush(gradient), 2); // Border thickness = 2
    painter.setPen(pen);
    QRect borderRect = adjustedRect.adjusted(1, 1, -1, -1);
    painter.drawRoundedRect(borderRect, 15, 15);

    QToolButton::paintEvent(event);
}
