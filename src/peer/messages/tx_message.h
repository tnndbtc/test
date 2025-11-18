// ============= tx_message.h =============
#ifndef TX_MESSAGE_H
#define TX_MESSAGE_H

#include "peer/peer_message.h"
#include "blockcore/transaction.h"
#include <cstdint>
#include <memory>
#include <string>

/**
 * @class CTxMessage
 * @brief Type-safe TX message implementation
 *
 * TX messages transmit serialized transaction data between peers.
 * Sent in response to GETDATA requests for transaction objects.
 *
 * Payload format: [N bytes serialized_transaction]
 *
 * Usage:
 *   auto p_tx = std::make_shared<CTransaction>(...);
 *   CTxMessage tx_msg(p_tx, LOCALNET_MAGIC);
 *   std::string serialized = tx_msg.Serialize();
 */
class CTxMessage : public CPeerMessage {
private:
    std::shared_ptr<CTransaction> m_p_tx;  ///< Transaction object

public:
    /**
     * @brief Constructor with transaction and magic
     */
    CTxMessage(std::shared_ptr<CTransaction> p_tx, uint32_t n_magic);

    /**
     * @brief Get message type (always returns "tx")
     */
    std::string GetType() const override;

    /**
     * @brief Serialize transaction data to binary payload
     */
    std::vector<uint8_t> SerializePayload() const override;

    /**
     * @brief Deserialize binary payload to transaction data
     */
    bool DeserializePayload(const std::vector<uint8_t>& payload) override;

    /**
     * @brief Create a copy of this message
     */
    std::unique_ptr<CPeerMessage> Clone() const override;

    /**
     * @brief Validate message (checks data not empty)
     */

    /**
     * @brief Get transaction object
     */
    std::shared_ptr<CTransaction> GetTransaction() const;
};

#endif // TX_MESSAGE_H
