#pragma once

#include "../Module.hpp"

class TntTimerModule : public Module {
public:
    TntTimerModule();
    ~TntTimerModule() override;

    void onInit() override;
};
