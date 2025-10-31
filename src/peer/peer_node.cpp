// ============= peer_node.cpp =============
#include "peer/peer_node.h"

// CPeerNode implementation

CPeerNode::CPeerNode()
    : str_address(""), n_port(0), n_connection_time(0), d_ping_roundtrip_time(0.0), f_inbound(false) {
}

CPeerNode::CPeerNode(const std::string& str_addr, int n_port_num)
    : str_address(str_addr), n_port(n_port_num), n_connection_time(0), d_ping_roundtrip_time(0.0), f_inbound(false) {
}

std::string CPeerNode::GetAddress() const {
    return str_address;
}

int CPeerNode::GetPort() const {
    return n_port;
}

std::string CPeerNode::GetIdentifier() const {
    return str_address + ":" + std::to_string(n_port);
}

bool CPeerNode::IsValid() const {
    return !str_address.empty() && n_port > 0 && n_port <= 65535;
}

bool CPeerNode::operator==(const CPeerNode& other) const {
    return str_address == other.str_address && n_port == other.n_port;
}

bool CPeerNode::operator!=(const CPeerNode& other) const {
    return !(*this == other);
}

bool CPeerNode::operator<(const CPeerNode& other) const {
    if (str_address != other.str_address) {
        return str_address < other.str_address;
    }
    return n_port < other.n_port;
}

std::string CPeerNode::GetInfo() const {
    std::string str_info = "{\n";
    str_info += "  \"address\": \"" + str_address + "\",\n";
    str_info += "  \"port\": " + std::to_string(n_port) + ",\n";
    str_info += "  \"identifier\": \"" + GetIdentifier() + "\",\n";
    str_info += "  \"connection_time\": " + std::to_string(n_connection_time) + ",\n";
    str_info += "  \"ping_roundtrip_time\": " + std::to_string(d_ping_roundtrip_time) + ",\n";
    str_info += "  \"inbound\": " + std::string(f_inbound ? "true" : "false") + "\n";
    str_info += "}";
    return str_info;
}

int64_t CPeerNode::GetConnectionTime() const {
    return n_connection_time;
}

void CPeerNode::SetConnectionTime(int64_t n_time) {
    n_connection_time = n_time;
}

double CPeerNode::GetPingRoundtripTime() const {
    return d_ping_roundtrip_time;
}

void CPeerNode::SetPingRoundtripTime(double d_time) {
    d_ping_roundtrip_time = d_time;
}

bool CPeerNode::IsInbound() const {
    return f_inbound;
}

void CPeerNode::SetInbound(bool f_is_inbound) {
    f_inbound = f_is_inbound;
}
