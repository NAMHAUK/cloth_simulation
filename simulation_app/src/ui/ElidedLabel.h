#pragma once

#include <QLabel>

class ElidedLabel final : public QLabel
{
public:
    explicit ElidedLabel(const QString& text, QWidget* parent = nullptr) : QLabel(parent) { set_text(text); }

    void set_text(const QString& text)
    {
        text_ = text;
        setToolTip(text);
        update_text();
    }

protected:
    void resizeEvent(QResizeEvent* event) override
    {
        QLabel::resizeEvent(event);
        update_text();
    }

private:
    void update_text() { QLabel::setText(fontMetrics().elidedText(text_, Qt::ElideRight, width())); }

    QString text_;
};
