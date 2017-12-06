#include "NatSwitch.hh"

#include "Common.hh"
#include "Controller.hh"
#include "SwitchConnection.hh"
#include "FlowFwd.hh"
#include "Flow.hh"
#include "PacketParser.hh"

#include "api/Packet.hh"
#include "oxm/openflow_basic.hh"

#include <unordered_map>
#include <iostream>
#include <sstream>
#include <functional>
#include <chrono>

REGISTER_APPLICATION(NatSwitch, {"controller", ""})

using namespace runos;

static const ethaddr empty_mac = ethaddr("00:00:00:00:00:00");


struct IpPortPair {
        std::string ip;
        ethaddr mac; // meta
        uint16_t port;

        IpPortPair() : ip(""), mac(empty_mac), port(0) {}

        IpPortPair(const std::string& str, const ethaddr& _mac = empty_mac)
        {
                ip = str.substr(0, str.find(":"));
                mac = _mac;
                port = (uint16_t) std::stoi(str.substr(str.find(":") + 1, str.length()));
        }

        IpPortPair(const std::string& _ip, uint16_t _port, const ethaddr& _mac = empty_mac)
                : ip(_ip), mac(_mac), port(_port) {}

        std::string toString() const {
                std::stringstream ss;
                ss << ip << ':' << port;
                return ss.str();
        }

        bool empty() const {
                return ip.empty() && port == 0;
        }

        bool operator==(const IpPortPair& p) const 
        {
                return ip == p.ip && port == p.port;
        }
};

// hash function for IpPortPair
namespace std {
        template<>
        class hash<IpPortPair> {
        public:
                size_t operator()(const IpPortPair& p) const
                {
                        return std::hash<std::string>()(p.toString());
                }
        };
}

class NatTable {
private:
        // External IP list
        std::vector<std::string> m_ip_pool;

        // Address mapping
        // m_output - (local) => (external)
        // m_input - (external) => (local)
        std::unordered_map<IpPortPair, IpPortPair> m_input, m_output;

        // Creates new mapping for local ip:port pair and external ip
        IpPortPair createMapping(IpPortPair src, std::string ip_dst)
        {
                // calculate hash to pick IP and port from pool
                size_t h = std::hash<IpPortPair>()(src) + std::hash<std::string>()(ip_dst);

                // ip is hash % sizeof (ip_pool)
                // port is (hash % 65521) + 10, 65521 is a prime number
                IpPortPair result;

                // check if this pair is free now
                do {
                        result = IpPortPair(m_ip_pool[h % m_ip_pool.size()], h % 65521);
                        h++;
                } while (m_input.find(result) != m_input.end());

                // reserve this pair
                m_input[result] = src;
                m_output[src] = result;

                return result;
        }
        
public:
        NatTable() {}

        void addExternalIp(std::string ip)
        {
                m_ip_pool.push_back(ip);
        }

        // Takes destination IP/port pair and returns external pair
        IpPortPair mapToOutput(const std::string& ip_src, uint16_t tcp_src, const std::string& ip_dst, const ethaddr& mac = empty_mac)
        {
                IpPortPair src(ip_src, tcp_src, mac);

                // try to find existing mapping for given internal pair
                if (m_output.find(src) != m_input.end()) {
                        return m_output[src];
                } else {
                        return createMapping(src, ip_dst);
                }

                return IpPortPair();
        }

        IpPortPair mapToOutput(IPv4Addr ip_src, uint16_t tcp_src, IPv4Addr ip_dst, const ethaddr& mac = empty_mac)
        {
                std::stringstream ss1, ss2;
                ss1 << ip_src;
                ss2 << ip_dst;
                return mapToOutput(ss1.str(), tcp_src, ss2.str(), mac);
        }

        // Takes external IP/port pair and redirects it to local ones.
        // If there's no suitable rule, returns empty IP+port pair
        IpPortPair mapToInput(std::string ip_dst, uint16_t tcp_dst)
        {
                IpPortPair dst(ip_dst, tcp_dst);

                if (m_input.find(dst) != m_input.end()) {
                        return m_input[dst];
                }

                return IpPortPair();
        }

        IpPortPair mapToInput(IPv4Addr ip_dst, uint16_t tcp_dst)
        {
                std::stringstream ss;
                ss << ip_dst;
                return mapToInput(ss.str(), tcp_dst);
        }

        // Removes redirect rule for specific local ip+port pair
};

// Timeout types
typedef std::chrono::duration<int> seconds;

void NatSwitch::init(Loader *loader, const Config& rootConfig)
{
        Controller *ctrl = Controller::get(loader);
        const Config& config = config_cd(rootConfig, "natswitch");

        unsigned int nat_dpid = (unsigned int) config_get(config, "switch_dpid", 2);
        std::string local_subnet_str = config_get(config, "local_subnet", "10.0.0.0/16");
        const auto &addr_pool = config.at("address_pool").array_items();
        unsigned int local_port = config_get(config, "local_port", 1);
        unsigned int external_port = config_get(config, "external_port", 2);
        std::string nat_mac = config_get(config, "mac", "11:22:33:44:55:66");
        unsigned int nat_idle_timeout = config_get(config, "idle_timeout", 180);
        unsigned int nat_hard_timeout = config_get(config, "hard_timeout", 1800);

        LOG(INFO) << "Need to start NAT switch at switch " << nat_dpid;
        LOG(INFO) << "Local port: " << local_port << ", external port: " << external_port;
        LOG(INFO) << "NAT MAC: " << nat_mac;
        LOG(INFO) << "Idle timeout: " << nat_idle_timeout;
        LOG(INFO) << "Address pool:";
        for (auto &ip : addr_pool) {
                LOG(INFO) << ip.string_value();
        }

        // create redirection table
        NatTable *nat_map = new NatTable;
        for (auto &ip : addr_pool) {
                nat_map->addExternalIp(ip.string_value());
        }

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
                const auto ofb_eth_src = oxm::eth_src();
                const auto ofb_ip_src = oxm::ipv4_src();
                const auto ofb_tcp_src = oxm::tcp_src();
                const auto ofb_ip_dst = oxm::ipv4_dst();
                const auto ofb_tcp_dst = oxm::tcp_dst();

                return [=](Packet& pkt, FlowPtr flow, Decision decision) {
                        // check that this is an TCP packet (or ICMP for nice pinging)
                        if (not pkt.test(oxm::eth_type() == 0x0800)) {
                                return decision;
                        }
                        if (not pkt.test(oxm::ip_proto() == 6)) {
                                return decision;
                        }

                        IPv4Addr ip_src, ip_dst;
                        int tcp_src = -1, tcp_dst = -1;
                        auto in_port = pkt.load(ofb_in_port);
                        ethaddr eth_src = pkt.load(ofb_eth_src);

                        ip_src = pkt.load(ofb_ip_src);
                        tcp_src = pkt.load(ofb_tcp_src);
                        ip_dst = pkt.load(ofb_ip_dst);
                        tcp_dst = pkt.load(ofb_tcp_dst);
                        
                        // check subnet
                        if (in_port == local_port) {
                                // packet from local subnet
                                LOG(INFO) << "Got PacketIn from local subnet " << ip_src << ":" << tcp_src;

                                // find a mapping for it
                                IpPortPair mapTo = nat_map->mapToOutput(ip_src, tcp_src, ip_dst, eth_src);

                                LOG(INFO) << "Map it to external pair " << mapTo.toString();

                                // modify current packet and unicast it
                                pkt.modify(oxm::eth_src() << "11:22:33:44:55:66"); //nat_mac);
                                pkt.modify(oxm::ipv4_src() << mapTo.ip);
                                pkt.modify(oxm::tcp_src() << mapTo.port);

                                return decision
                                        .unicast(external_port)
                                        .idle_timeout(seconds(nat_idle_timeout))
                                        .hard_timeout(seconds(nat_hard_timeout))
                                        .return_();
                        } else if (in_port == external_port) {
                                // packet from external subnet
                                LOG(INFO) << "Got PacketIn from external subnet " << ip_src << ":" << tcp_src;

                                // find a mapping
                                IpPortPair mapTo = nat_map->mapToInput(ip_dst, tcp_dst);

                                // drop it if mapping is empty
                                if (mapTo.empty()) {
                                        LOG(INFO) << "Unknown connection; drop it";
                                        return decision
                                                .drop()
                                                .idle_timeout(seconds(nat_idle_timeout))
                                                .hard_timeout(seconds(nat_hard_timeout))
                                                .return_();
                                } else {
                                        // if there's a mapping, apply it
                                        pkt.modify(oxm::eth_dst() << mapTo.mac);
                                        pkt.modify(oxm::ipv4_dst() << mapTo.ip);
                                        pkt.modify(oxm::tcp_dst() << mapTo.port);

                                        return decision
                                                .unicast(local_port)
                                                .idle_timeout(seconds(nat_idle_timeout))
                                                .hard_timeout(seconds(nat_hard_timeout))
                                                .return_();
                                }
                        } else {
                                LOG(WARNING) << "Unknown packet port: " << in_port;
                        }
                        

                        return decision;
                };
        });
}

void NatSwitch::onSwitchUp(SwitchConnectionPtr conn, of13::FeaturesReply fr)
{
        LOG(INFO) << "Switch appeared: " << conn->dpid();
}
