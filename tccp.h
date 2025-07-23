// Copyright (c) 2025 The TCCP developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_TCCP_H
#define BITCOIN_TCCP_H

#include <cstddef>
#include <vector>
#include <uint256.h>

// Forward declarations to reduce compilation dependencies.
class CBlock;
class CBlockIndex;
namespace Consensus { struct Params; }


/**
 * TCCP encapsulates the logic for the Tightly-Coupled Capability Proof soft fork.
 *
 * This module provides the functions necessary to compute and verify the TCCP,
 * a consensus rule that requires block producers to prove they can handle
 * blocks up to the network's maximum configured size.
 */
namespace TCCP {

/**
 * Computes the TCCP proof P_i = MT-root(G(H(H(B_{i-1}) || M_real), SIZE_max)).
 * This function is used by miners to generate the proof for a new block.
 *
 * @param prevBlockHash The hash of the previous block header (H(B_{i-1})).
 * @param merkleRoot The provisional Merkle root of the real transaction set (M_real).
 * @param params The consensus parameters for the current chain, which contain the TCCP challenge size.
 * @return The 256-bit TCCP proof (P_i).
 */
uint256 ComputeProof(const uint256& prevBlockHash, const uint256& merkleRoot, const Consensus::Params& params);

/**
 * Verifies the TCCP commitment within a given block.
 * This is the primary consensus-enforcement function called during block validation.
 * It reconstructs the expected proof and compares it against the one committed
 * in the block's coinbase transaction.
 *
 * @param block The block to be verified.
 * @param pindexPrev A pointer to the previous block's index entry in the chain. Must not be null.
 * @param params The consensus parameters for the current chain.
 * @return True if the block contains a valid TCCP commitment, false otherwise.
 */
bool VerifyBlock(const CBlock& block, const CBlockIndex* pindexPrev, const Consensus::Params& params);

} // namespace TCCP

#endif // BITCOIN_TCCP_H
