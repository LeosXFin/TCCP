// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2022 The Bitcoin Core developers
// Copyright (c) 2025 The TCCP developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CONSENSUS_CONSENSUS_H
#define BITCOIN_CONSENSUS_CONSENSUS_H

#include <cstdlib>
#include <cstdint>
#include <vector>

/**
 * A 4-byte identifier for TCCP commitments in OP_RETURN outputs.
 * Spells 'TCCP' in ASCII.
 */
static const std::vector<unsigned char> TCCP_MAGIC_BYTES = {0x54, 0x43, 0x43, 0x50}; // 'TCCP'

/**
 * Parameters for a BIP8-style softfork deployment.
 */
struct BIP8Deployment {
    /** The bit to signal for this deployment (0-28). */
    int bit;
    /** The block height at which signaling begins. */
    int nStartHeight;
    /** The block height at which the fork is forced to activate if not already locked-in. */
    int nTimeoutHeight;
};

namespace Consensus {
/**
 * Parameters that influence chain consensus.
 */
struct Params {
    uint256 hashGenesisBlock;
    // ... other consensus parameters like 'nSubsidyHalvingInterval' etc.

    /**
     * TCCP (Tightly-Coupled Capability Proof) softfork deployment parameters.
     * Governed by BIP8.
     */
    BIP8Deployment tccp_deployment;

    /**
     * The target size in bytes for the TCCP virtual challenge block.
     * This should correspond to the network's maximum block size.
     */
    size_t nTCCPChallengeSize;

    // ... other methods ...
};
} // namespace Consensus

#endif // BITCOIN_CONSENSUS_CONSENSUS_H
