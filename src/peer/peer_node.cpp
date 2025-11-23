// ============= peer_node.cpp =============
#include "peer/peer_node.h"
#include "logger/logger.h"

// CPeerNode implementation

CPeerNode::CPeerNode()
    : p_socket(nullptr),
      f_connected(false),
      m_handshake_state(HandshakeState::NONE),
      n_last_ping_nonce(0),
      m_last_ping_send_time(),
      str_address(""),
      n_port(0),
      n_connection_time(0),
      d_ping_roundtrip_time(0.0),
      f_inbound(false),
      n_protocol_version(0),
      n_services(0) {
}

CPeerNode::CPeerNode(const std::string& str_addr, int n_port_num)
    : p_socket(nullptr),
      f_connected(false),
      m_handshake_state(HandshakeState::NONE),
      n_last_ping_nonce(0),
      m_last_ping_send_time(),
      str_address(str_addr),
      n_port(n_port_num),
      n_connection_time(0),
      d_ping_roundtrip_time(0.0),
      f_inbound(false),
      n_protocol_version(0),
      n_services(0) {
}

CPeerNode::CPeerNode(const std::string& str_addr, int n_port_num,
                     std::shared_ptr<boost::asio::ip::tcp::socket> socket)
    : p_socket(socket),
      // f_connected(socket && socket->is_open()),
      f_connected(false),
      m_handshake_state(HandshakeState::NONE),
      n_last_ping_nonce(0),
      m_last_ping_send_time(),
      str_address(str_addr),
      n_port(n_port_num),
      n_connection_time(0),
      d_ping_roundtrip_time(0.0),
      f_inbound(false),
      n_protocol_version(0),
      n_services(0) {
}
/* not needed for now
// Move constructor
CPeerNode::CPeerNode(CPeerNode&& other) noexcept {
    std::lock_guard<std::mutex> lock(other.cs_peer_node);

    // Move socket and connection state
    p_socket = std::move(other.p_socket);
    f_connected = other.f_connected;
    n_last_ping_nonce = other.n_last_ping_nonce;
    m_last_ping_send_time = other.m_last_ping_send_time;

    // Move peer metadata
    str_address = std::move(other.str_address);
    n_port = other.n_port;
    n_connection_time = other.n_connection_time;
    d_ping_roundtrip_time = other.d_ping_roundtrip_time;
    f_inbound = other.f_inbound;
    n_protocol_version = other.n_protocol_version;
    n_services = other.n_services;

    // Reset other to safe state
    other.f_connected = false;
    other.n_last_ping_nonce = 0;
    other.n_port = 0;
}
*/
/* not needed for now
// Move assignment operator
CPeerNode& CPeerNode::operator=(CPeerNode&& other) noexcept {
    if (this != &other) {
        // Close existing socket before moving
        if (p_socket && p_socket->is_open()) {
            boost::system::error_code ec;
            p_socket->shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
            p_socket->close(ec);
        }

        // Lock both mutexes to avoid deadlock
        std::lock(cs_peer_node, other.cs_peer_node);
        std::lock_guard<std::mutex> lock1(cs_peer_node, std::adopt_lock);
        std::lock_guard<std::mutex> lock2(other.cs_peer_node, std::adopt_lock);

        // Move socket and connection state
        p_socket = std::move(other.p_socket);
        f_connected = other.f_connected;
        n_last_ping_nonce = other.n_last_ping_nonce;
        m_last_ping_send_time = other.m_last_ping_send_time;

        // Move peer metadata
        str_address = std::move(other.str_address);
        n_port = other.n_port;
        n_connection_time = other.n_connection_time;
        d_ping_roundtrip_time = other.d_ping_roundtrip_time;
        f_inbound = other.f_inbound;
        n_protocol_version = other.n_protocol_version;
        n_services = other.n_services;

        // Reset other to safe state
        other.f_connected = false;
        other.n_last_ping_nonce = 0;
        other.n_port = 0;
    }
    return *this;
}
*/
// Destructor - close socket if still open
CPeerNode::~CPeerNode() {
    if (p_socket && p_socket->is_open()) {
        boost::system::error_code ec;
        p_socket->shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
        p_socket->close(ec);
        // Errors during shutdown are non-fatal (connection may already be closed)
    }
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

std::shared_ptr<boost::asio::ip::tcp::socket> CPeerNode::GetSocket() const {
    std::lock_guard<std::mutex> lock(cs_peer_node);
    return p_socket;
}

bool CPeerNode::IsConnected() const {
    std::lock_guard<std::mutex> lock(cs_peer_node);
    return f_connected;
}

void CPeerNode::SetConnected(bool f_is_connected) {
    std::lock_guard<std::mutex> lock(cs_peer_node);
    std::string str_f_connected = (f_connected ? "true" : "false");
    LOG_TRACE("CPeerNode::SetConnected before set: " + str_f_connected);
    f_connected = f_is_connected;
    str_f_connected = (f_connected ? "true" : "false");
    LOG_TRACE("CPeerNode::SetConnected after set: " + str_f_connected);
}

uint32_t CPeerNode::GetLastPingNonce() const {
    std::lock_guard<std::mutex> lock(cs_peer_node);
    return n_last_ping_nonce;
}

void CPeerNode::SetLastPingNonce(uint32_t n_nonce) {
    std::lock_guard<std::mutex> lock(cs_peer_node);
    n_last_ping_nonce = n_nonce;
}

std::chrono::steady_clock::time_point CPeerNode::GetLastPingSendTime() const {
    std::lock_guard<std::mutex> lock(cs_peer_node);
    return m_last_ping_send_time;
}

void CPeerNode::SetLastPingSendTime(std::chrono::steady_clock::time_point time) {
    std::lock_guard<std::mutex> lock(cs_peer_node);
    m_last_ping_send_time = time;
}

bool CPeerNode::AddHandshakeFlag(uint8_t flag) {
    std::string str_id = GetIdentifier();
    std::lock_guard<std::mutex> lock(cs_peer_node);
    if (m_handshake_state & flag) {
        LOG_WARN("Duplicate handshake message received.  Reject peer: " + str_id);
        return false;
    }
    m_handshake_state |= flag;
    if (m_handshake_state == HandshakeState::COMPLETE) {
        f_connected = true;
        LOG_INFO("Handshake complete with peer " + str_id);
        return true;
    }
    LOG_TRACE("Handshake flag is set to: " + std::to_string(m_handshake_state) + " for peer: " + str_id);
    return true;
}

bool CPeerNode::IsHandshakeComplete() const {
    std::lock_guard<std::mutex> lock(cs_peer_node);
    return m_handshake_state == HandshakeState::COMPLETE;
}
