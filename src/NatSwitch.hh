#pragma once

#include "Application.hh"
#include "Loader.hh"
#include "OFTransaction.hh"

class NatSwitch : public Application {
Q_OBJECT
SIMPLE_APPLICATION(NatSwitch, "natswitch")

public:
        void init(Loader* loader, const Config &config) override;

public slots:
        void onSwitchUp(SwitchConnectionPtr conn, of13::FeaturesReply fr);
};
