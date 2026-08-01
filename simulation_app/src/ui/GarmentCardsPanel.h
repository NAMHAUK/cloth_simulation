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
    void set_color_edit_callback(ColorEditCallback callback);

    // Card state
    void set_card(GarmentLayer layer, const QString& garment_name, const glm::vec3& color);
    void confirm_card(GarmentLayer layer);
    void set_card_color(GarmentLayer layer, const glm::vec3& color);
    void clear_card(GarmentLayer layer);
    void clear_cards();
    void clear_edit_highlight();

private:
    struct Card final
    {
        glm::vec3 color{1.0f};
        bool has_garment = false;
        bool is_confirmed = false;
        QFrame* frame = nullptr;
        QPushButton* color_button = nullptr;
        QLabel* name_label = nullptr;
    };

    // Internal UI
    void create_card(GarmentLayer layer);
    void request_color_edit(GarmentLayer layer);
    void update_card_color(GarmentLayer layer);
    void update_card_visibility();

    std::array<Card, 2> cards_{};
    std::optional<GarmentLayer> edit_card_layer_;
    ColorEditCallback color_edit_callback_;
};
