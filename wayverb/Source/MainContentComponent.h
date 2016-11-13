#pragma once

#include "HelpWindow.h"
#include "LeftPanel.h"

#include "scene/master.h"

class MainContentComponent final : public Component,
                                   public SettableHelpPanelClient {
public:
    MainContentComponent(wayverb::combined::model::app& app);

    void paint(Graphics& g) override;
    void resized() override;

private:
    StretchableLayoutManager layout_manager_;

    LeftPanel left_panel_;
    StretchableLayoutResizerBar resizer_bar_;
    scene::master right_panel_;
};
