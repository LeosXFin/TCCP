// Copyright (c) 2025 The TCCP developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <tccp.h>

#include <consensus/consensus.h>
#include <consensus/merkle.h>
#include <hash.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <util/bit_cast.h>

#include <vector>

namespace { // Anonymous namespace for internal linkage (helper functions)

/**
 * A simple, deterministic Linear Congruential Generator (LCG).
 * Its purpose is to generate a predictable stream of pseudo-random data
 * based on a seed. This does NOT need to be cryptographically secure; it only
 * needs to be perfectly deterministic and consistent across all clients.
 */
class LCG {
private:
    uint64_t m_state;
public:
    explicit LCG(const uint256& seed) {
        // Initialize the LCG state from the first 8 bytes of the seed.
        m_state = bit_cast<uint64_t>(seed);
    }
    uint32_t next() {
        m_state = 1664525 * m_state + 1013904223;
        return m_state >> 32;
    }
};

/**
 * Computes the TCCP seed S_i = H(H(B_{i-1}) || M_real).
 */
uint256 CalculateSeed(const uint256& prevBlockHash, const uint256& merkleRoot) {
    CHash256 hasher;
    hasher.Write(prevBlockHash.begin(), prevBlockHash.size());
    hasher.Write(merkleRoot.begin(), merkleRoot.size());
    return hasher.GetHash();
}

/**
 * Implements the G(S_i, SIZE_max) function to create a deterministic
 * set of virtual transactions for the capability challenge.
 */
std::vector<CTransactionRef> GenerateVirtualChallenge(const uint256& seed, size_t maxSize) {
    std::vector<CTransactionRef> virtualTxs;
    LCG prng(seed);
    size_t currentSize = 0;

    while (true) {
        CMutableTransaction mtx;
        mtx.nVersion = 1;
        mtx.nLockTime = 0;

        // Create a deterministic input.
        COutPoint prevOut(uint256(), prng.next() % 100);
        mtx.vin.resize(1);
        mtx.vin[0].prevout = prevOut;
        mtx.vin[0].scriptSig = CScript() << prng.next() << prng.next();

        // Create a deterministic OP_RETURN output.
        std::vector<uint8_t> data(32);
        for (size_t i = 0; i < data.size(); ++i) { data[i] = prng.next() % 256; }
        mtx.vout.resize(1);
        mtx.vout[0].nValue = Amount::zero();
        mtx.vout[0].scriptPubKey = CScript() << OP_RETURN << data;

        CTransactionRef tx = MakeTransactionRef(mtx);
        size_t txSize = tx->GetTotalSize();

        if (currentSize + txSize > maxSize) {
            break;
        }

        virtualTxs.push_back(tx);
        currentSize += txSize;
    }
    return virtualTxs;
}

} // end anonymous namespace

namespace TCCP {

uint256 ComputeProof(const uint256& prevBlockHash, const uint256& merkleRoot, const Consensus::Params& params) {
    // 1. Calculate the seed S_i
    const uint256 seed = CalculateSeed(prevBlockHash, merkleRoot);

    // 2. Generate the virtual challenge V_chal using the challenge size from consensus params.
    const auto v_chal = GenerateVirtualChallenge(seed, params.nTCCPChallengeSize);

    // 3. Compute the proof P_i, which is the Merkle root of the virtual transaction hashes.
    if (v_chal.empty()) {
        return uint256();
    }
    
    std::vector<uint256> leaves;
    leaves.reserve(v_chal.size());
    for (const auto& tx : v_chal) {
        leaves.push_back(tx->GetHash());
    }
    return ComputeMerkleRoot(leaves);
}

bool VerifyBlock(const CBlock& block, const CBlockIndex* pindexPrev, const Consensus::Params& params) {
    // Genesis block and null pindexPrev are exempt from TCCP rules.
    if (!pindexPrev) {
        return true;
    }

    const CTransaction& coinbaseTx = *block.vtx[0];
    uint256 submittedProof;
    int commitmentOutIndex = -1;

    // 1. Find the TCCP commitment in the coinbase transaction.
    // We must find exactly one validly formatted commitment.
    for (size_t i = 0; i < coinbaseTx.vout.size(); ++i) {
        const CScript& script = coinbaseTx.vout[i].scriptPubKey;
        
        // Check for the exact format: OP_RETURN <0x24 (36)> <4-byte magic> <32-byte proof>
        // Total script size: 1 (OP_RETURN) + 1 (PUSHDATA) + 36 (payload) = 38 bytes.
        if (script.IsUnspendable() && script.size() == 38 && script[0] == OP_RETURN && script[1] == 0x24) {
            std::vector<unsigned char> data(script.begin() + 2, script.end());
            if (std::vector<unsigned char>(data.begin(), data.begin() + 4) == TCCP_MAGIC_BYTES) {
                // Fail if more than one commitment is found.
                if (commitmentOutIndex != -1) return false;
                
                submittedProof = uint256(std::vector<unsigned char>(data.begin() + 4, data.end()));
                commitmentOutIndex = i;
            }
        }
    }

    // Fail if no commitment was found where one is required.
    if (commitmentOutIndex == -1) return false;

    // 2. Reconstruct the provisional Merkle Root (M_real).
    // This is the most critical step of verification. The block header contains the *final*
    // Merkle root, which includes the TCCP commitment. But the proof was *generated* from a
    // provisional root calculated *before* the commitment was added.
    // To verify, we must recreate this provisional root by calculating the block's Merkle root
    // as if the TCCP commitment output did not exist.
    uint256 M_real_reconstructed;
    {
        // Create a mutable copy of the coinbase transaction.
        CMutableTransaction mtx_coinbase(coinbaseTx);
        // Erase the commitment output we found.
        mtx_coinbase.vout.erase(mtx_coinbase.vout.begin() + commitmentOutIndex);
        
        // Create a temporary list of block transactions, replacing the original coinbase
        // with our modified one.
        std::vector<CTransactionRef> vtx_temp = block.vtx;
        vtx_temp[0] = MakeTransactionRef(std::move(mtx_coinbase));
        
        // Calculate the Merkle root from this temporary transaction set to get M_real.
        M_real_reconstructed = BlockMerkleRoot(vtx_temp);
    }
    
    // 3. Calculate the expected proof using the reconstructed provisional Merkle root.
    const uint256 expectedProof = ComputeProof(pindexPrev->GetBlockHash(), M_real_reconstructed, params);

    // 4. Finally, compare the submitted proof with the one we calculated. They must match.
    return submittedProof == expectedProof;
}

} // namespace TCCP
