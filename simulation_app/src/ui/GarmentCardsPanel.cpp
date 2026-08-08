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

const QString color_button_style = QStringLiteral(R"(
    QPushButton {
        background-color: %1;
        border: 3px solid %2;
        border-radius: 19px;
    }
    QPushButton:hover {
        border-color: #1f6feb;
    }
)");
}

GarmentCardsPanel::GarmentCardsPanel(QWidget* parent) : QWidget(parent)
{
    auto* cards_layout = new QVBoxLayout(this);
    cards_layout->setContentsMargins(0, 0, 0, 0);
    cards_layout->setSpacing(8);

    setup_card(GarmentLayer::Lower);
    setup_card(GarmentLayer::Upper);

    update_card_visibility();
}

// Initialization
void GarmentCardsPanel::setup_card(GarmentLayer layer)
{
    Card& card = cards_[layer];
    card.frame = new QFrame(this);
    card.frame->setFixedHeight(garment_card_height);

    auto* card_layout = new QGridLayout(card.frame);
    card_layout->setContentsMargins(0, 0, 0, 0);

    card.name_label = new QLabel(card.frame);
    card.name_label->setObjectName("garmentCardBody");
    card.name_label->setContentsMargins(garment_color_button_size / 2 + 10, 0, 12, 0);
    auto* body_row_layout = new QHBoxLayout();
    body_row_layout->setContentsMargins(garment_color_button_size / 2, 0, 0, 0);
    body_row_layout->addWidget(card.name_label);

    card.color_button = new QPushButton(card.frame);
    card.color_button->setFixedSize(garment_color_button_size, garment_color_button_size);
    card.color_button->setToolTip("Choose garment color");

    card_layout->addLayout(body_row_layout, 0, 0);
    card_layout->addWidget(card.color_button, 0, 0, Qt::AlignLeft | Qt::AlignVCenter);
    card.color_button->raise();
    layout()->addWidget(card.frame);

    connect(card.color_button, &QPushButton::clicked, this, [this, layer]() { request_color_edit(layer); });
}

// Card State
void GarmentCardsPanel::set_card(GarmentLayer layer, const QString& garment_name)
{
    Card& card = cards_[layer];
    card.color = glm::vec3{1.0f};
    card.state = CardState::Placement;
    card.name_label->setText(garment_name);
    card.name_label->setToolTip(garment_name);

    update_color_button(layer);
    update_card_visibility();
}

void GarmentCardsPanel::set_card_color(GarmentLayer layer, const glm::vec3& color)
{
    cards_[layer].color = color;
    update_color_button(layer);
}

void GarmentCardsPanel::confirm_cards()
{
    for (Card& card : cards_) {
        if (card.state == CardState::Placement) {
            card.state = CardState::Confirmed;
        }
    }

    update_card_visibility();
}

void GarmentCardsPanel::clear_card(GarmentLayer layer)
{
    cards_[layer].state = CardState::Empty;
    update_card_visibility();
}

void GarmentCardsPanel::clear_unconfirmed_cards()
{
    for (Card& card : cards_) {
        if (card.state == CardState::Placement) {
            card.state = CardState::Empty;
        }
    }

    update_card_visibility();
}

void GarmentCardsPanel::clear_cards()
{
    for (Card& card : cards_) {
        card.state = CardState::Empty;
    }

    update_card_visibility();
}

void GarmentCardsPanel::update_card_visibility()
{
    const bool lower_visible = cards_[GarmentLayer::Lower].state == CardState::Confirmed;
    const bool upper_visible = cards_[GarmentLayer::Upper].state == CardState::Confirmed;

    cards_[GarmentLayer::Lower].frame->setVisible(lower_visible);
    cards_[GarmentLayer::Upper].frame->setVisible(upper_visible);

    setVisible(lower_visible || upper_visible);
}

// Color Editing
void GarmentCardsPanel::request_color_edit(GarmentLayer layer)
{
    edit_card_layer_ = layer;
    update_color_button(GarmentLayer::Lower);
    update_color_button(GarmentLayer::Upper);

    Q_EMIT color_edit_requested(layer, cards_[layer].color);
}

void GarmentCardsPanel::clear_edit_highlight()
{
    edit_card_layer_.reset();
    update_color_button(GarmentLayer::Lower);
    update_color_button(GarmentLayer::Upper);
}

void GarmentCardsPanel::update_color_button(GarmentLayer layer)
{
    const Card& card = cards_[layer];
    const QColor button_color = QColor::fromRgbF(card.color.r, card.color.g, card.color.b);
    const QString border_color = edit_card_layer_ == layer ? "#1f6feb" : "#111111";
    card.color_button->setStyleSheet(color_button_style.arg(button_color.name(), border_color));
}
