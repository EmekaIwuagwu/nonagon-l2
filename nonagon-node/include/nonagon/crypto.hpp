#pragma once

#include <array>
#include <cstdint>
#include <vector>
#include <string>
#include <algorithm>

namespace nonagon {
namespace crypto {

/**
 * @brief Blake2b-256 hash implementation
 * 
 * Nonagon uses Blake2b-256 for all hashing operations for:
 * - Cardano compatibility (uses Blake2b)
 * - Superior performance vs SHA-256
 * - No length-extension attacks
 */
class Blake2b256 {
public:
    static constexpr size_t HASH_SIZE = 32;
    using HashBytes = std::array<uint8_t, HASH_SIZE>;

    static HashBytes hash(const uint8_t* data, size_t len);
    static HashBytes hash(const std::vector<uint8_t>& data);
    static HashBytes hash(const std::string& data);

    // Merkle tree operations
    static HashBytes merkle_root(const std::vector<HashBytes>& leaves);
    static std::vector<HashBytes> merkle_proof(const std::vector<HashBytes>& leaves, size_t index);
    static bool verify_merkle_proof(const HashBytes& leaf, const std::vector<HashBytes>& proof, 
                                     size_t index, const HashBytes& root);

private:
    static HashBytes combine_hashes(const HashBytes& left, const HashBytes& right);
};

/**
 * @brief Ed25519 signature scheme
 * 
 * Used for:
 * - Transaction signing (Cardano compatible)
 * - Block producer attestations
 * - Bridge message signing
 */
class Ed25519 {
public:
    static constexpr size_t PUBLIC_KEY_SIZE = 32;
    static constexpr size_t SECRET_KEY_SIZE = 64;
    static constexpr size_t SIGNATURE_SIZE = 64;
    static constexpr size_t SEED_SIZE = 32;

    using PublicKey = std::array<uint8_t, PUBLIC_KEY_SIZE>;
    using SecretKey = std::array<uint8_t, SECRET_KEY_SIZE>;
    using Signature = std::array<uint8_t, SIGNATURE_SIZE>;
    using Seed = std::array<uint8_t, SEED_SIZE>;

    struct KeyPair {
        PublicKey public_key;
        SecretKey secret_key;
    };

    static KeyPair generate_keypair();
    static KeyPair keypair_from_seed(const Seed& seed);
    static Signature sign(const uint8_t* message, size_t len, const SecretKey& sk);
    static bool verify(const uint8_t* message, size_t len, const Signature& sig, const PublicKey& pk);
};

/**
 * @brief Bech32 encoding/decoding for Cardano-compatible addresses
 */
class Bech32 {
public:
    static std::string encode(const std::string& hrp, const std::vector<uint8_t>& data);
    static bool decode(const std::string& str, std::string& hrp, std::vector<uint8_t>& data);
    
    // Nonagon address prefixes
    static constexpr const char* MAINNET_PREFIX = "addr1";
    static constexpr const char* TESTNET_PREFIX = "addr_test1";
};

} // namespace crypto
} // namespace nonagon
