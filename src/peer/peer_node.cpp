// ============= peer_node.cpp =============
#include "peer/peer_node.h"

// CPeerNode implementation

CPeerNode::CPeerNode()
    : str_address(""), n_port(0), n_connection_time(0), d_ping_roundtrip_time(0.0), f_inbound(false), n_protocol_version(0), n_services(0) {
}

CPeerNode::CPeerNode(const std::string& str_addr, int n_port_num)
    : str_address(str_addr), n_port(n_port_num), n_connection_time(0), d_ping_roundtrip_time(0.0), f_inbound(false), n_protocol_version(0), n_services(0) {
}

// Copy constructor
CPeerNode::CPeerNode(const CPeerNode& other) {
    std::lock_guard<std::mutex> lock(other.cs_peer_node);
    str_address = other.str_address;
    n_port = other.n_port;
    n_connection_time = other.n_connection_time;
    d_ping_roundtrip_time = other.d_ping_roundtrip_time;
    f_inbound = other.f_inbound;
    n_protocol_version = other.n_protocol_version;
    n_services = other.n_services;
}

// Copy assignment operator
CPeerNode& CPeerNode::operator=(const CPeerNode& other) {
    if (this != &other) {
        // Lock both mutexes to avoid deadlock (lock in consistent order)
        std::lock(cs_peer_node, other.cs_peer_node);
        std::lock_guard<std::mutex> lock1(cs_peer_node, std::adopt_lock);
        std::lock_guard<std::mutex> lock2(other.cs_peer_node, std::adopt_lock);

        str_address = other.str_address;
        n_port = other.n_port;
        n_connection_time = other.n_connection_time;
        d_ping_roundtrip_time = other.d_ping_roundtrip_time;
        f_inbound = other.f_inbound;
        n_protocol_version = other.n_protocol_version;
        n_services = other.n_services;
    }
    return *this;
}

// Move constructor
CPeerNode::CPeerNode(CPeerNode&& other) noexcept {
    std::lock_guard<std::mutex> lock(other.cs_peer_node);
    str_address = std::move(other.str_address);
    n_port = other.n_port;
    n_connection_time = other.n_connection_time;
    d_ping_roundtrip_time = other.d_ping_roundtrip_time;
    f_inbound = other.f_inbound;
    n_protocol_version = other.n_protocol_version;
    n_services = other.n_services;
}

// Move assignment operator
CPeerNode& CPeerNode::operator=(CPeerNode&& other) noexcept {
    if (this != &other) {
        // Lock both mutexes to avoid deadlock
        std::lock(cs_peer_node, other.cs_peer_node);
        std::lock_guard<std::mutex> lock1(cs_peer_node, std::adopt_lock);
        std::lock_guard<std::mutex> lock2(other.cs_peer_node, std::adopt_lock);

        str_address = std::move(other.str_address);
        n_port = other.n_port;
        n_connection_time = other.n_connection_time;
        d_ping_roundtrip_time = other.d_ping_roundtrip_time;
        f_inbound = other.f_inbound;
        n_protocol_version = other.n_protocol_version;
        n_services = other.n_services;
    }
    return *this;
}

std::string CPeerNode::GetAddress() const {
    std::lock_guard<std::mutex> lock(cs_peer_node);
    return str_address;
}

int CPeerNode::GetPort() const {
    std::lock_guard<std::mutex> lock(cs_peer_node);
    return n_port;
}

std::string CPeerNode::GetIdentifier() const {
    std::lock_guard<std::mutex> lock(cs_peer_node);
    return str_address + ":" + std::to_string(n_port);
}

bool CPeerNode::IsValid() const {
    std::lock_guard<std::mutex> lock(cs_peer_node);
    return !str_address.empty() && n_port > 0 && n_port <= 65535;
}

bool CPeerNode::operator==(const CPeerNode& other) const {
    if (this == &other) {
        return true;
    }

    // Lock both mutexes in a consistent order to avoid deadlock
    std::lock(cs_peer_node, other.cs_peer_node);
    std::lock_guard<std::mutex> lock1(cs_peer_node, std::adopt_lock);
    std::lock_guard<std::mutex> lock2(other.cs_peer_node, std::adopt_lock);

    return str_address == other.str_address &&
           n_port == other.n_port &&
           n_connection_time == other.n_connection_time &&
           d_ping_roundtrip_time == other.d_ping_roundtrip_time &&
           f_inbound == other.f_inbound &&
           n_protocol_version == other.n_protocol_version &&
           n_services == other.n_services;
}

bool CPeerNode::operator!=(const CPeerNode& other) const {
    return !(*this == other);
}

bool CPeerNode::operator<(const CPeerNode& other) const {
    if (this == &other) {
        return false;
    }

    // Lock both mutexes in a consistent order to avoid deadlock
    std::lock(cs_peer_node, other.cs_peer_node);
    std::lock_guard<std::mutex> lock1(cs_peer_node, std::adopt_lock);
    std::lock_guard<std::mutex> lock2(other.cs_peer_node, std::adopt_lock);

    if (str_address != other.str_address) {
        return str_address < other.str_address;
    }
    return n_port < other.n_port;
}

std::string CPeerNode::GetInfo() const {
    // call this before holding the lock
    std::string str_identifier = GetIdentifier();
    std::lock_guard<std::mutex> lock(cs_peer_node);
    std::string str_info = "{\n";
    str_info += "  \"address\": \"" + str_address + "\",\n";
    str_info += "  \"port\": " + std::to_string(n_port) + ",\n";
    str_info += "  \"identifier\": \"" + str_identifier + "\",\n";
    str_info += "  \"connection_time\": " + std::to_string(n_connection_time) + ",\n";
    str_info += "  \"ping_roundtrip_time\": " + std::to_string(d_ping_roundtrip_time) + ",\n";
    str_info += "  \"inbound\": " + std::string(f_inbound ? "true" : "false") + "\n";
    str_info += "}";
    return str_info;
}

int64_t CPeerNode::GetConnectionTime() const {
    std::lock_guard<std::mutex> lock(cs_peer_node);
    return n_connection_time;
}

void CPeerNode::SetConnectionTime(int64_t n_time) {
    std::lock_guard<std::mutex> lock(cs_peer_node);
    n_connection_time = n_time;
}

double CPeerNode::GetPingRoundtripTime() const {
    std::lock_guard<std::mutex> lock(cs_peer_node);
    return d_ping_roundtrip_time;
}

void CPeerNode::SetPingRoundtripTime(double d_time) {
    std::lock_guard<std::mutex> lock(cs_peer_node);
    d_ping_roundtrip_time = d_time;
}

bool CPeerNode::IsInbound() const {
    std::lock_guard<std::mutex> lock(cs_peer_node);
    return f_inbound;
}

void CPeerNode::SetInbound(bool f_is_inbound) {
    std::lock_guard<std::mutex> lock(cs_peer_node);
    f_inbound = f_is_inbound;
}

int32_t CPeerNode::GetProtocolVersion() const {
    std::lock_guard<std::mutex> lock(cs_peer_node);
    return n_protocol_version;
}

void CPeerNode::SetProtocolVersion(int32_t n_version) {
    std::lock_guard<std::mutex> lock(cs_peer_node);
    n_protocol_version = n_version;
}

uint64_t CPeerNode::GetServices() const {
    std::lock_guard<std::mutex> lock(cs_peer_node);
    return n_services;
}

void CPeerNode::SetServices(uint64_t n_service_flags) {
    std::lock_guard<std::mutex> lock(cs_peer_node);
    n_services = n_service_flags;
}
