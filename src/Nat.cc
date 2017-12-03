#include "Nat.hh"

#include "Common.hh"

REGISTER_APPLICATION(NatSwitch, {""})

void NatSwitch::init(Loader *loader, const Config& config)
{
        LOG(INFO) << "NatSwitch want to say 'Hello'!";
}
