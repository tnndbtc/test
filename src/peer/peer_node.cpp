// ============= peer_node.cpp =============
#include "peer/peer_node.h"

// CPeerNode implementation

CPeerNode::CPeerNode()
    : str_address(""), n_port(0) {
}

CPeerNode::CPeerNode(const std::string& str_addr, int n_port_num)
    : str_address(str_addr), n_port(n_port_num) {
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
    str_info += "  \"identifier\": \"" + GetIdentifier() + "\"\n";
    str_info += "}";
    return str_info;
}
