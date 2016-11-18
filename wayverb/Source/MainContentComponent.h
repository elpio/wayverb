#pragma once

#include "HelpWindow.h"

#include "left_bar/master.h"
#include "scene/master.h"

class MainContentComponent final : public Component,
                                   public SettableHelpPanelClient {
public:
    MainContentComponent(wayverb::combined::model::app& app,
                         engine_message_queue& queue);

    void paint(Graphics& g) override;
    void resized() override;

private:
    StretchableLayoutManager layout_manager_;

    left_bar::master left_bar_master_;
    StretchableLayoutResizerBar resizer_bar_;
    scene::master scene_master_;

    Component* components_to_position_[3];
    static constexpr size_t num_components_ =
            std::extent<decltype(components_to_position_)>{};
};
