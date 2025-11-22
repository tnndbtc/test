// ============= get_public_ip.h =============
#ifndef GET_PUBLIC_IP_H
#define GET_PUBLIC_IP_H

#include <string>
#include <vector>

/**
 * @brief Discover public IP address using STUN protocol
 * @param vec_stun_addresses List of STUN server addresses in "hostname:port" format
 * @param n_timeout_ms Timeout in milliseconds for each STUN request (default: 3000ms)
 * @return Public IP address as string, or "127.0.0.1" on failure
 *
 * Attempts to contact STUN servers in order until one succeeds.
 * Uses RFC 5389 STUN Binding Request to discover reflexive transport address (public IP).
 * Thread-safe, can be called from any thread.
 *
 * Example usage:
 *   std::vector<std::string> stun_servers = {
 *       "stun.l.google.com:19302",
 *       "stun.stunprotocol.org:3478"
 *   };
 *   std::string public_ip = GetPublicIP(stun_servers);
 *   if (public_ip != "127.0.0.1") {
 *       LOG_INFO("Discovered public IP: " + public_ip);
 *   }
 */
std::string GetPublicIP(const std::vector<std::string>& vec_stun_addresses,
                        int n_timeout_ms = 3000);

#endif // GET_PUBLIC_IP_H
