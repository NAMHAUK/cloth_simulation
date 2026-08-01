#include "ui/GarmentCardsPanel.h"

#include <utility>

#include <QColor>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QString>
#include <QVBoxLayout>

namespace {
constexpr int garment_color_button_size = 38;
constexpr int garment_card_height = garment_color_button_size;
}

GarmentCardsPanel::GarmentCardsPanel(QWidget* parent) : QWidget(parent)
{
    auto* cards_layout = new QVBoxLayout(this);
    cards_layout->setContentsMargins(0, 0, 0, 0);
    cards_layout->setSpacing(8);
    setStyleSheet(R"(
        #garmentCard {
            background: transparent;
            border: none;
        }
        #garmentCardBody {
            background-color: #3a3a3a;
            border: none;
            border-radius: 8px;
        }
        #garmentCardBody QLabel {
            background: transparent;
            border: none;
            color: white;
            font-size: 16px;
            font-weight: 600;
        }
    )");

    for (std::size_t index = 0; index < cards_.size(); ++index) {
        create_card(static_cast<GarmentLayer>(index));
    }

    update_card_visibility();
}

void GarmentCardsPanel::set_color_edit_callback(ColorEditCallback callback)
{
    color_edit_callback_ = std::move(callback);
}

// Card state
void GarmentCardsPanel::set_card(GarmentLayer layer, const QString& garment_name, const glm::vec3& color)
{
    Card& card = cards_[layer];
    card.color = color;
    card.has_garment = true;
    card.is_confirmed = false;
    card.name_label->setText(garment_name);
    card.name_label->setToolTip(garment_name);
    update_card_color(layer);
    update_card_visibility();
}

void GarmentCardsPanel::confirm_card(GarmentLayer layer)
{
    if (!cards_[layer].has_garment) {
        return;
    }

    cards_[layer].is_confirmed = true;
    update_card_visibility();
}

void GarmentCardsPanel::set_card_color(GarmentLayer layer, const glm::vec3& color)
{
    if (!cards_[layer].has_garment) {
        return;
    }

    cards_[layer].color = color;
    update_card_color(layer);
}

void GarmentCardsPanel::clear_card(GarmentLayer layer)
{
    Card& card = cards_[layer];
    card.has_garment = false;
    card.is_confirmed = false;
    card.name_label->clear();
    update_card_visibility();
}

void GarmentCardsPanel::clear_cards()
{
    for (Card& card : cards_) {
        card.has_garment = false;
        card.is_confirmed = false;
        card.name_label->clear();
    }
    update_card_visibility();
}

void GarmentCardsPanel::clear_edit_highlight()
{
    edit_card_layer_.reset();
    for (std::size_t index = 0; index < cards_.size(); ++index) {
        update_card_color(static_cast<GarmentLayer>(index));
    }
}

// Internal UI
void GarmentCardsPanel::create_card(GarmentLayer layer)
{
    Card& card = cards_[layer];
    card.frame = new QFrame(this);
    card.frame->setObjectName("garmentCard");
    card.frame->setFixedHeight(garment_card_height);

    auto* card_layout = new QGridLayout(card.frame);
    card_layout->setContentsMargins(0, 0, 0, 0);

    auto* card_body = new QFrame(card.frame);
    card_body->setObjectName("garmentCardBody");
    card_body->setFixedHeight(garment_card_height);

    auto* body_layout = new QHBoxLayout(card_body);
    body_layout->setContentsMargins(garment_color_button_size / 2 + 10, 0, 12, 0);

    auto* body_row_layout = new QHBoxLayout();
    body_row_layout->setContentsMargins(garment_color_button_size / 2, 0, 0, 0);
    body_row_layout->addWidget(card_body);

    card.color_button = new QPushButton(card.frame);
    card.color_button->setFixedSize(garment_color_button_size, garment_color_button_size);
    card.color_button->setToolTip("Choose garment color");
    card.name_label = new QLabel(card_body);

    body_layout->addWidget(card.name_label);
    card_layout->addLayout(body_row_layout, 0, 0);
    card_layout->addWidget(card.color_button, 0, 0, Qt::AlignLeft | Qt::AlignVCenter);
    card.color_button->raise();
    layout()->addWidget(card.frame);

    connect(card.color_button, &QPushButton::clicked, this, [this, layer]() { request_color_edit(layer); });
}

void GarmentCardsPanel::request_color_edit(GarmentLayer layer)
{
    const Card& card = cards_[layer];
    if (!card.has_garment || !card.is_confirmed) {
        return;
    }

    edit_card_layer_ = layer;
    for (std::size_t card_index = 0; card_index < cards_.size(); ++card_index) {
        update_card_color(static_cast<GarmentLayer>(card_index));
    }
    if (color_edit_callback_) {
        color_edit_callback_(layer, card.color);
    }
}

void GarmentCardsPanel::update_card_color(GarmentLayer layer)
{
    if (!cards_[layer].has_garment) {
        return;
    }

    const Card& card = cards_[layer];
    const QColor button_color = QColor::fromRgbF(card.color.r, card.color.g, card.color.b);
    const QString border_color = edit_card_layer_ == layer ? "#1f6feb" : "#111111";
    card.color_button->setStyleSheet(QString(R"(
        QPushButton {
            background-color: rgb(%1, %2, %3);
            border: 3px solid %4;
            border-radius: 19px;
            padding: 0;
        }
        QPushButton:hover {
            border-color: #1f6feb;
        }
    )")
                                         .arg(button_color.red())
                                         .arg(button_color.green())
                                         .arg(button_color.blue())
                                         .arg(border_color));
}

void GarmentCardsPanel::update_card_visibility()
{
    bool has_visible_card = false;
    for (const Card& card : cards_) {
        const bool is_visible = card.has_garment && card.is_confirmed;
        card.frame->setVisible(is_visible);
        has_visible_card = has_visible_card || is_visible;
    }

    setVisible(has_visible_card);
}
