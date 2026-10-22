#pragma once

#include "../Module.hpp"

class ChatTimestampsModule : public Module {
public:
    ChatTimestampsModule();
    ~ChatTimestampsModule() override;

    void onInit() override;
};
