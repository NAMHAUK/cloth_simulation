#pragma once

#include "asset/AssetDataTypes.h"

#include <array>
#include <functional>
#include <optional>

#include <glm/vec3.hpp>

#include <QWidget>

class QFrame;
class QLabel;
class QPushButton;
class QString;

class GarmentCardsPanel final : public QWidget
{
public:
    using ColorEditCallback = std::function<void(GarmentLayer layer, const glm::vec3& color)>;

    explicit GarmentCardsPanel(QWidget* parent = nullptr);

    void set_card(GarmentLayer layer, const QString& garment_name);
    void set_card_color(GarmentLayer layer, const glm::vec3& color);
    void confirm_cards();
    void clear_card(GarmentLayer layer);
    void clear_unconfirmed_cards();
    void clear_cards();

    void clear_edit_highlight();

    void set_color_edit_callback(ColorEditCallback callback);

private:
    enum class CardState
    {
        Empty,
        Placement,
        Confirmed,
    };

    struct Card final
    {
        glm::vec3 color{1.0f};
        CardState state = CardState::Empty;
        QFrame* frame = nullptr;
        QPushButton* color_button = nullptr;
        QLabel* name_label = nullptr;
    };

    void setup_card(GarmentLayer layer);

    void update_card_visibility();

    void request_color_edit(GarmentLayer layer);
    void update_color_button(GarmentLayer layer);

    std::array<Card, 2> cards_{};
    std::optional<GarmentLayer> edit_card_layer_;
    ColorEditCallback color_edit_callback_;
};
