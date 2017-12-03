#include "NatSwitch.hh"

#include "Common.hh"
#include "Controller.hh"
#include "SwitchConnection.hh"


REGISTER_APPLICATION(NatSwitch, {"controller", ""})

void NatSwitch::init(Loader *loader, const Config& rootConfig)
{
        Controller *ctrl = Controller::get(loader);
        const Config& config = config_cd(rootConfig, "natswitch");

        int nat_dpid = config_get(config, "switch_dpid", 2);
        const std::string& local_subnet = config_get(config, "local_subnet", "10.0.0.0/16");
        const auto &addr_pool = config.at("address_pool").array_items();

        LOG(INFO) << "Need to start NAT switch at switch " << nat_dpid;
        LOG(INFO) << "Local subnet is " << local_subnet;
        LOG(INFO) << "Address pool:";
        for (auto &ip : addr_pool) {
                LOG(INFO) << ip.string_value();
        }

}

void NatSwitch::onSwitchUp(SwitchConnectionPtr conn, of13::FeaturesReply fr)
{
        LOG(INFO) << "Switch appeared: " << conn->dpid();
}
