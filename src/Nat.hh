#pragma once

#include "Application.hh"
#include "Loader.hh"

class NatSwitch : public Application {
SIMPLE_APPLICATION(NatSwitch, "natswitch")

public:
        void init(Loader* loader, const Config &config) override;
};
