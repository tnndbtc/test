// ============= get_public_ip.cpp =============
/**
 * @file get_public_ip.cpp
 * @brief Implementation of STUN client for public IP discovery
 *
 * Implements RFC 5389 STUN (Session Traversal Utilities for NAT) protocol
 * for discovering public IP address behind NAT. Uses Boost.Asio UDP sockets
 * for network communication.
 */

#include "get_public_ip.h"
#include "logger/logger.h"
#include <boost/asio.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/asio/steady_timer.hpp>
#include <array>
#include <cstring>
#include <random>
#include <sstream>

// STUN protocol constants (RFC 5389)
constexpr uint16_t STUN_BINDING_REQUEST  = 0x0001;
constexpr uint16_t STUN_BINDING_RESPONSE = 0x0101;
constexpr uint32_t STUN_MAGIC_COOKIE     = 0x2112A442;
constexpr uint16_t STUN_ATTR_XOR_MAPPED_ADDRESS = 0x0020;
constexpr uint8_t  STUN_ADDRESS_FAMILY_IPV4 = 0x01;
constexpr size_t   STUN_HEADER_SIZE = 20;
constexpr size_t   STUN_MAX_MESSAGE_SIZE = 548;

// STUN message header (20 bytes, RFC 5389 Section 6)
#pragma pack(push, 1)
struct StunHeader {
    uint16_t message_type;      // 0x0001 for Binding Request
    uint16_t message_length;    // Length of attributes (0 for request)
    uint32_t magic_cookie;      // Fixed: 0x2112A442
    uint8_t  transaction_id[12]; // Random 96-bit identifier
};
#pragma pack(pop)

// STUN attribute header (4 bytes)
#pragma pack(push, 1)
struct StunAttributeHeader {
    uint16_t type;              // Attribute type
    uint16_t length;            // Attribute value length
};
#pragma pack(pop)

// XOR-MAPPED-ADDRESS IPv4 format (8 bytes value)
#pragma pack(push, 1)
struct StunXorMappedAddressIPv4 {
    uint8_t  reserved;          // Must be 0
    uint8_t  family;            // 0x01 for IPv4
    uint16_t x_port;            // Port XORed with magic cookie high 16 bits
    uint32_t x_address;         // IPv4 XORed with magic cookie
};
#pragma pack(pop)

/**
 * @brief Address parser result
 */
struct StunServerAddress {
    std::string str_hostname;
    std::string str_port;
};

/**
 * @brief Parse "hostname:port" format
 * @param str_address STUN server address string
 * @return Parsed hostname and port
 */
static StunServerAddress ParseStunAddress(const std::string& str_address) {
    size_t colon_pos = str_address.rfind(':');
    if (colon_pos == std::string::npos) {
        // Default STUN port if not specified
        return { str_address, "3478" };
    }
    return {
        str_address.substr(0, colon_pos),
        str_address.substr(colon_pos + 1)
    };
}

/**
 * @brief Generate random bytes for transaction ID
 * @param p_buffer Buffer to fill with random bytes
 * @param n_length Number of bytes to generate
 */
static void GenerateRandomBytes(uint8_t* p_buffer, size_t n_length) {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dist(0, 255);

    for (size_t i = 0; i < n_length; i++) {
        p_buffer[i] = static_cast<uint8_t>(dist(gen));
    }
}

/**
 * @brief Build STUN Binding Request message
 * @param buffer Buffer to store the request (must be at least 20 bytes)
 */
static void BuildStunBindingRequest(std::array<uint8_t, STUN_HEADER_SIZE>& buffer) {
    StunHeader* p_header = reinterpret_cast<StunHeader*>(buffer.data());

    p_header->message_type = htons(STUN_BINDING_REQUEST);      // 0x0001
    p_header->message_length = htons(0);                        // No attributes
    p_header->magic_cookie = htonl(STUN_MAGIC_COOKIE);         // 0x2112A442

    // Generate random transaction ID (96 bits)
    GenerateRandomBytes(p_header->transaction_id, 12);
}

/**
 * @brief Parse STUN Binding Response and extract public IP
 * @param response_buffer Buffer containing STUN response
 * @param n_length Length of response data
 * @param request_buffer Original request buffer (for transaction ID validation)
 * @return Public IP address as string, or "127.0.0.1" on parse error
 */
static std::string ParseStunBindingResponse(
    const std::array<uint8_t, STUN_MAX_MESSAGE_SIZE>& response_buffer,
    size_t n_length,
    const std::array<uint8_t, STUN_HEADER_SIZE>& request_buffer
) {
    if (n_length < STUN_HEADER_SIZE) {
        LOG_WARN("STUN response too short: " + std::to_string(n_length) + " bytes");
        return "127.0.0.1";
    }

    const StunHeader* p_header = reinterpret_cast<const StunHeader*>(response_buffer.data());
    const StunHeader* p_req_header = reinterpret_cast<const StunHeader*>(request_buffer.data());

    // Validate response
    if (ntohs(p_header->message_type) != STUN_BINDING_RESPONSE) {
        LOG_WARN("Invalid STUN message type: " + std::to_string(ntohs(p_header->message_type)));
        return "127.0.0.1";
    }

    if (ntohl(p_header->magic_cookie) != STUN_MAGIC_COOKIE) {
        LOG_WARN("Invalid STUN magic cookie");
        return "127.0.0.1";
    }

    if (std::memcmp(p_header->transaction_id, p_req_header->transaction_id, 12) != 0) {
        LOG_WARN("STUN transaction ID mismatch");
        return "127.0.0.1";
    }

    // Parse attributes
    size_t offset = STUN_HEADER_SIZE;
    uint16_t attr_length = ntohs(p_header->message_length);

    while (offset + 4 <= n_length && offset - STUN_HEADER_SIZE < attr_length) {
        const StunAttributeHeader* p_attr =
            reinterpret_cast<const StunAttributeHeader*>(response_buffer.data() + offset);

        uint16_t attr_type = ntohs(p_attr->type);
        uint16_t attr_len = ntohs(p_attr->length);

        if (attr_type == STUN_ATTR_XOR_MAPPED_ADDRESS && attr_len == 8) {
            // Found XOR-MAPPED-ADDRESS
            const StunXorMappedAddressIPv4* p_xor_addr =
                reinterpret_cast<const StunXorMappedAddressIPv4*>(response_buffer.data() + offset + 4);

            if (p_xor_addr->family == STUN_ADDRESS_FAMILY_IPV4) {
                // Decode XORed address
                uint32_t magic = STUN_MAGIC_COOKIE;
                uint32_t ip_addr = ntohl(p_xor_addr->x_address) ^ magic;

                // Convert to dotted decimal
                std::ostringstream oss;
                oss << ((ip_addr >> 24) & 0xFF) << "."
                    << ((ip_addr >> 16) & 0xFF) << "."
                    << ((ip_addr >> 8) & 0xFF) << "."
                    << (ip_addr & 0xFF);

                return oss.str();
            }
        }

        // Move to next attribute (align to 4-byte boundary)
        offset += 4 + ((attr_len + 3) & ~3);
    }

    LOG_WARN("No XOR-MAPPED-ADDRESS attribute found in STUN response");
    return "127.0.0.1";
}

/**
 * @brief Query single STUN server for public IP
 * @param str_address STUN server address in "hostname:port" format
 * @param n_timeout_ms Timeout in milliseconds
 * @return Public IP address as string, or "127.0.0.1" on failure
 */
static std::string QueryStunServer(const std::string& str_address, int n_timeout_ms) {
    // Parse hostname:port
    StunServerAddress server = ParseStunAddress(str_address);

    try {
        boost::asio::io_context io_context;
        boost::asio::ip::udp::socket socket(io_context, boost::asio::ip::udp::v4());

        // Resolve hostname
        boost::asio::ip::udp::resolver resolver(io_context);
        boost::asio::ip::udp::resolver::results_type endpoints =
            resolver.resolve(boost::asio::ip::udp::v4(),
                           server.str_hostname,
                           server.str_port);

        if (endpoints.empty()) {
            LOG_WARN("Failed to resolve STUN server: " + server.str_hostname);
            return "127.0.0.1";
        }

        boost::asio::ip::udp::endpoint server_endpoint = *endpoints.begin();

        // Build and send STUN Binding Request
        std::array<uint8_t, STUN_HEADER_SIZE> request_buffer;
        BuildStunBindingRequest(request_buffer);

        socket.send_to(boost::asio::buffer(request_buffer), server_endpoint);

        // Use async receive with deadline timer for proper timeout handling
        std::array<uint8_t, STUN_MAX_MESSAGE_SIZE> response_buffer;
        boost::asio::ip::udp::endpoint sender_endpoint;
        boost::system::error_code ec;
        size_t n_bytes_received = 0;
        bool f_timeout = false;

        // Set up deadline timer
        boost::asio::steady_timer timer(io_context);
        timer.expires_after(std::chrono::milliseconds(n_timeout_ms));
        timer.async_wait([&](const boost::system::error_code& error) {
            if (!error) {
                // Timer expired before receive completed
                f_timeout = true;
                socket.cancel();  // Cancel pending receive operation
            }
        });

        // Set up async receive
        socket.async_receive_from(
            boost::asio::buffer(response_buffer),
            sender_endpoint,
            [&](const boost::system::error_code& error, size_t bytes_transferred) {
                ec = error;
                n_bytes_received = bytes_transferred;
                timer.cancel();  // Cancel timer on successful receive
            }
        );

        // Run io_context until both operations complete
        io_context.run();

        if (f_timeout || ec == boost::asio::error::operation_aborted) {
            LOG_WARN("STUN query timeout for " + str_address);
            return "127.0.0.1";
        }

        if (ec) {
            LOG_WARN("STUN network error for " + str_address + ": " + ec.message());
            return "127.0.0.1";
        }

        // Parse response and extract public IP
        return ParseStunBindingResponse(response_buffer, n_bytes_received, request_buffer);

    } catch (const boost::system::system_error& e) {
        LOG_WARN("STUN network error for " + str_address + ": " + e.what());
        return "127.0.0.1";
    } catch (const std::exception& e) {
        LOG_WARN("STUN query failed for " + str_address + ": " + e.what());
        return "127.0.0.1";
    }
}

/**
 * @brief Discover public IP address using STUN protocol
 * @param vec_stun_addresses List of STUN server addresses in "hostname:port" format
 * @param n_timeout_ms Timeout in milliseconds for each STUN request
 * @return Public IP address as string, or "127.0.0.1" on failure
 */
std::string GetPublicIP(const std::vector<std::string>& vec_stun_addresses,
                        int n_timeout_ms) {
    if (vec_stun_addresses.empty()) {
        LOG_WARN("No STUN servers configured, using 127.0.0.1");
        return "127.0.0.1";
    }

    // Try each STUN server in order
    for (const auto& str_address : vec_stun_addresses) {
        std::string str_result = QueryStunServer(str_address, n_timeout_ms);
        if (str_result != "127.0.0.1") {
            LOG_INFO("Successfully discovered public IP from " + str_address + ": " + str_result);
            return str_result;
        }
    }

    LOG_ERROR("All STUN servers failed, returning 127.0.0.1");
    return "127.0.0.1";
}
