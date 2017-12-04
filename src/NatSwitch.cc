#include "NatSwitch.hh"

#include "Common.hh"
#include "Controller.hh"
#include "SwitchConnection.hh"
#include "FlowFwd.hh"
#include "Flow.hh"

#include "api/Packet.hh"
#include "oxm/openflow_basic.hh"

#include <unordered_map>
#include <iostream>

REGISTER_APPLICATION(NatSwitch, {"controller", ""})

using namespace runos;

struct IpPortPair {
        IPv4Addr addr;
        uint16_t port;
};

class NatTable {
};


void NatSwitch::init(Loader *loader, const Config& rootConfig)
{
        Controller *ctrl = Controller::get(loader);
        const Config& config = config_cd(rootConfig, "natswitch");

        unsigned int nat_dpid = (unsigned int) config_get(config, "switch_dpid", 2);
        const std::string& local_subnet_str = config_get(config, "local_subnet", "10.0.0.0/16");
        const auto &addr_pool = config.at("address_pool").array_items();
        unsigned int local_port = config_get(config, "local_port", 1);
        unsigned int external_port = config_get(config, "external_port", 2);

        LOG(INFO) << "Need to start NAT switch at switch " << nat_dpid;
        LOG(INFO) << "Local port: " << local_port << ", external port: " << external_port;
        LOG(INFO) << "Address pool:";
        for (auto &ip : addr_pool) {
                LOG(INFO) << ip.string_value();
        }

        // reserve a table for us
        uint8_t table_no = ctrl->reserveTable();

        // subscribe on controller's PacketIns
        ctrl->registerHandler("natswitch", 
        [=](SwitchConnectionPtr conn) -> PacketMissHandler {
                if (conn->dpid() != nat_dpid) {
                        LOG(INFO) << "Detected connection with " << conn->dpid() << " != " << nat_dpid << "; ignore it";
                        return [=](Packet&, FlowPtr, Decision d) {
                                return d;
                        };
                }

                const auto ofb_in_port = oxm::in_port();
                const auto ofb_ip_src = oxm::ipv4_src();
                const auto ofb_tcp_src = oxm::tcp_src();

                return [=](Packet& pkt, FlowPtr flow, Decision decision) {
                        // check that this is an TCP packet (or ICMP for nice pinging)
                        if (not pkt.test(oxm::eth_type() == 0x0800)) {
                                return decision;
                        }
                        if (not pkt.test(oxm::ip_proto() == 6)) {
                                return decision;
                        }

                        IPv4Addr ip_src;
                        int tcp_src = -1;
                        auto in_port = pkt.load(ofb_in_port);

                        ip_src = pkt.load(ofb_ip_src);
                        tcp_src = pkt.load(ofb_tcp_src);
                        
                        // check subnet
                        if (in_port == local_port) {
                                // packet from local to external subnet
                                LOG(INFO) << "Got PacketIn from local subnet " << ip_src << ":" << tcp_src;
                        } else if (in_port == external_port) {
                                LOG(INFO) << "Got PacketIn from external subnet " << ip_src << ":" << tcp_src;
                        }
                        

                        return decision;
                };
        });
}

void NatSwitch::onSwitchUp(SwitchConnectionPtr conn, of13::FeaturesReply fr)
{
        LOG(INFO) << "Switch appeared: " << conn->dpid();
}
