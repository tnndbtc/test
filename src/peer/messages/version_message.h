// ============= version_message.h =============
#ifndef VERSION_MESSAGE_H
#define VERSION_MESSAGE_H

#include "peer/peer_message.h"
#include <cstdint>
#include <memory>
#include <string>

/**
 * @class CVersionMessage
 * @brief Type-safe VERSION message implementation
 *
 * VERSION messages exchange protocol version and client information during handshake.
 * Payload is a simple string containing version information.
 *
 * Payload format: [N bytes version_string]
 *
 * Usage:
 *   CVersionMessage version("bweave/1.0.0", LOCALNET_MAGIC);
 *   std::string serialized = version.Serialize();
 */
class CVersionMessage : public CPeerMessage {
private:
    std::string m_str_version_info;  ///< Version information string

public:
    /**
     * @brief Constructor with version info and magic
     */
    CVersionMessage(const std::string& str_version_info, uint32_t n_magic);

    /**
     * @brief Get message type (always returns "version")
     */
    std::string GetType() const override;

    /**
     * @brief Serialize version info to binary payload
     */
    std::vector<uint8_t> SerializePayload() const override;

    /**
     * @brief Deserialize binary payload to version info
     */
    bool DeserializePayload(const std::vector<uint8_t>& payload) override;

    /**
     * @brief Create a copy of this message
     */
    std::unique_ptr<CPeerMessage> Clone() const override;

    /**
     * @brief Validate message (always true for version)
     */

    /**
     * @brief Get version information string
     */
    const std::string& GetVersionInfo() const;

    /**
     * @brief Set version information string
     */
    void SetVersionInfo(const std::string& str_version_info);
};

#endif // VERSION_MESSAGE_H
