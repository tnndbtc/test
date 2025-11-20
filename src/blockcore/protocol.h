#ifndef BLOCKCORE_PROTOCOL_H
#define BLOCKCORE_PROTOCOL_H

#include <cstdint>

// Protocol version for the blockweave P2P network
static const int32_t PROTOCOL_VERSION = 1;

// Service flags - 64-bit bitmap indicating node capabilities
// Nodes advertise their services during VERSION/VERACK handshake

// NODE_NETWORK: Node has a complete copy of the blockchain and can serve historical blocks
static const uint64_t NODE_NETWORK = (1ULL << 0);

#endif // BLOCKCORE_PROTOCOL_H
