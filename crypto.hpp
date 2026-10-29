#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <limits.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <unistd.h>
#endif

namespace crypto {

inline std::string toHex(const uint8_t* data, size_t len) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (size_t i = 0; i < len; ++i) {
        oss << std::setw(2) << static_cast<int>(data[i]);
    }
    return oss.str();
}

inline std::string toHex(const std::array<uint8_t, 32>& d) {
    return toHex(d.data(), d.size());
}

inline bool fromHex(const std::string& hex, uint8_t* out, size_t out_len) {
    if (hex.size() != out_len * 2) return false;
    for (size_t i = 0; i < out_len; ++i) {
        unsigned int byte = 0;
        std::istringstream iss(hex.substr(i * 2, 2));
        if (!(iss >> std::hex >> byte)) return false;
        out[i] = static_cast<uint8_t>(byte);
    }
    return true;
}

#ifndef _WIN32
// ============================================================================
// Linux: Use Official OpenSSL (libcrypto) for SHA-256, HMAC-SHA256 & Ed25519
// ============================================================================
class SHA256 {
public:
    SHA256() {
        ctx_ = EVP_MD_CTX_new();
        EVP_DigestInit_ex(ctx_, EVP_sha256(), nullptr);
    }
    ~SHA256() {
        if (ctx_) EVP_MD_CTX_free(ctx_);
    }
    SHA256(const SHA256&) = delete;
    SHA256& operator=(const SHA256&) = delete;

    void update(const uint8_t* data, size_t len) {
        EVP_DigestUpdate(ctx_, data, len);
    }

    void update(const std::string& s) {
        update(reinterpret_cast<const uint8_t*>(s.data()), s.size());
    }

    std::array<uint8_t, 32> digest() {
        std::array<uint8_t, 32> out{};
        unsigned int len = 0;
        EVP_DigestFinal_ex(ctx_, out.data(), &len);
        return out;
    }

    static std::string toHex(const uint8_t* data, size_t len) { return crypto::toHex(data, len); }
    static std::string toHex(const std::array<uint8_t, 32>& d) { return crypto::toHex(d); }
    static bool fromHex(const std::string& hex, uint8_t* out, size_t out_len) { return crypto::fromHex(hex, out, out_len); }

private:
    EVP_MD_CTX* ctx_ = nullptr;
};

inline std::string hmacSha256Hex(const std::string& key, const std::string& message) {
    uint8_t out[EVP_MAX_MD_SIZE];
    unsigned int out_len = 0;
    HMAC(EVP_sha256(),
         key.data(), static_cast<int>(key.size()),
         reinterpret_cast<const uint8_t*>(message.data()), message.size(),
         out, &out_len);
    return toHex(out, out_len);
}

namespace ed25519 {

inline void derivePublicKey(const uint8_t seed[32], uint8_t pk[32]) {
    EVP_PKEY* pkey = EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, nullptr, seed, 32);
    size_t pk_len = 32;
    EVP_PKEY_get_raw_public_key(pkey, pk, &pk_len);
    EVP_PKEY_free(pkey);
}

inline std::string signMessageHex(const uint8_t seed[32], const std::string& message) {
    EVP_PKEY* pkey = EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, nullptr, seed, 32);
    EVP_MD_CTX* md_ctx = EVP_MD_CTX_new();
    EVP_DigestSignInit(md_ctx, nullptr, nullptr, nullptr, pkey);
    uint8_t sig[64];
    size_t sig_len = 64;
    EVP_DigestSign(md_ctx, sig, &sig_len,
                   reinterpret_cast<const uint8_t*>(message.data()), message.size());
    EVP_MD_CTX_free(md_ctx);
    EVP_PKEY_free(pkey);
    return toHex(sig, 64);
}

} // namespace ed25519

#else
// ============================================================================
// Windows Cross-Compile Fallback (Zero-DLL Static Build)
// ============================================================================
class SHA256 {
public:
    SHA256() { reset(); }

    void reset() {
        state_[0] = 0x6a09e667u; state_[1] = 0xbb67ae85u;
        state_[2] = 0x3c6ef372u; state_[3] = 0xa54ff53au;
        state_[4] = 0x510e527fu; state_[5] = 0x9b05688cu;
        state_[6] = 0x1f83d9abu; state_[7] = 0x5be0cd19u;
        bitlen_ = 0; buflen_ = 0;
    }

    void update(const uint8_t* data, size_t len) {
        for (size_t i = 0; i < len; ++i) {
            buf_[buflen_++] = data[i];
            if (buflen_ == 64) {
                transform(buf_);
                bitlen_ += 512;
                buflen_ = 0;
            }
        }
    }

    void update(const std::string& s) {
        update(reinterpret_cast<const uint8_t*>(s.data()), s.size());
    }

    std::array<uint8_t, 32> digest() {
        std::array<uint8_t, 32> hash{};
        uint64_t total_bits = bitlen_ + static_cast<uint64_t>(buflen_) * 8;
        size_t i = buflen_;
        if (buflen_ < 56) {
            buf_[i++] = 0x80;
            while (i < 56) buf_[i++] = 0x00;
        } else {
            buf_[i++] = 0x80;
            while (i < 64) buf_[i++] = 0x00;
            transform(buf_);
            std::memset(buf_, 0, 56);
        }
        for (int b = 0; b < 8; ++b) {
            buf_[63 - b] = static_cast<uint8_t>((total_bits >> (b * 8)) & 0xFFu);
        }
        transform(buf_);
        for (int j = 0; j < 8; ++j) {
            hash[j * 4 + 0] = static_cast<uint8_t>((state_[j] >> 24) & 0xFFu);
            hash[j * 4 + 1] = static_cast<uint8_t>((state_[j] >> 16) & 0xFFu);
            hash[j * 4 + 2] = static_cast<uint8_t>((state_[j] >> 8) & 0xFFu);
            hash[j * 4 + 3] = static_cast<uint8_t>(state_[j] & 0xFFu);
        }
        return hash;
    }

    static std::string toHex(const uint8_t* data, size_t len) { return crypto::toHex(data, len); }
    static std::string toHex(const std::array<uint8_t, 32>& d) { return crypto::toHex(d); }
    static bool fromHex(const std::string& hex, uint8_t* out, size_t out_len) { return crypto::fromHex(hex, out, out_len); }

    static std::array<uint8_t, 32> compute(const std::string& input) {
        SHA256 ctx;
        ctx.update(input);
        return ctx.digest();
    }

private:
    uint32_t state_[8];
    uint64_t bitlen_;
    uint8_t buf_[64];
    size_t buflen_;

    static inline uint32_t rotr(uint32_t x, uint32_t n) { return (x >> n) | (x << (32 - n)); }

    void transform(const uint8_t block[64]) {
        static const uint32_t k[64] = {
            0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
            0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
            0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
            0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
            0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
            0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
            0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
            0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
        };
        uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<uint32_t>(block[i * 4 + 0]) << 24) |
                   (static_cast<uint32_t>(block[i * 4 + 1]) << 16) |
                   (static_cast<uint32_t>(block[i * 4 + 2]) << 8) |
                   (static_cast<uint32_t>(block[i * 4 + 3]));
        }
        for (int i = 16; i < 64; ++i) {
            uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3];
        uint32_t e = state_[4], f = state_[5], g = state_[6], h = state_[7];
        for (int i = 0; i < 64; ++i) {
            uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            uint32_t ch = (e & f) ^ ((~e) & g);
            uint32_t temp1 = h + S1 + ch + k[i] + w[i];
            uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t temp2 = S0 + maj;
            h = g; g = f; f = e; e = d + temp1; d = c; c = b; b = a; a = temp1 + temp2;
        }
        state_[0] += a; state_[1] += b; state_[2] += c; state_[3] += d;
        state_[4] += e; state_[5] += f; state_[6] += g; state_[7] += h;
    }
};

inline std::string hmacSha256Hex(const std::string& key, const std::string& message) {
    uint8_t k_ipad[64], k_opad[64], tk[32];
    const uint8_t* key_ptr = reinterpret_cast<const uint8_t*>(key.data());
    size_t key_len = key.size();
    if (key_len > 64) {
        auto hashed_key = SHA256::compute(key);
        std::memcpy(tk, hashed_key.data(), 32);
        key_ptr = tk;
        key_len = 32;
    }
    std::memset(k_ipad, 0x36, 64);
    std::memset(k_opad, 0x5c, 64);
    for (size_t i = 0; i < key_len; ++i) {
        k_ipad[i] ^= key_ptr[i];
        k_opad[i] ^= key_ptr[i];
    }
    SHA256 inner;
    inner.update(k_ipad, 64);
    inner.update(message);
    auto inner_digest = inner.digest();
    SHA256 outer;
    outer.update(k_opad, 64);
    outer.update(inner_digest.data(), 32);
    return SHA256::toHex(outer.digest());
}

class SHA512 {
public:
    SHA512() { reset(); }
    void reset() {
        state_[0] = 0x6a09e667f3bcc908ULL; state_[1] = 0xbb67ae8584caa73bULL;
        state_[2] = 0x3c6ef372fe94f82bULL; state_[3] = 0xa54ff53a5f1d36f1ULL;
        state_[4] = 0x510e527fade682d1ULL; state_[5] = 0x9b05688c2b3e6c1fULL;
        state_[6] = 0x1f83d9abfb41bd6bULL; state_[7] = 0x5be0cd19137e2179ULL;
        bitlen_lo_ = 0; bitlen_hi_ = 0; buflen_ = 0;
    }
    void update(const uint8_t* data, size_t len) {
        for (size_t i = 0; i < len; ++i) {
            buf_[buflen_++] = data[i];
            if (buflen_ == 128) {
                transform(buf_);
                uint64_t old = bitlen_lo_;
                bitlen_lo_ += 1024;
                if (bitlen_lo_ < old) bitlen_hi_++;
                buflen_ = 0;
            }
        }
    }
    std::array<uint8_t, 64> digest() {
        std::array<uint8_t, 64> out{};
        uint64_t total_lo = bitlen_lo_ + static_cast<uint64_t>(buflen_) * 8;
        uint64_t total_hi = bitlen_hi_ + (total_lo < bitlen_lo_ ? 1 : 0);
        size_t i = buflen_;
        if (buflen_ < 112) {
            buf_[i++] = 0x80;
            while (i < 112) buf_[i++] = 0x00;
        } else {
            buf_[i++] = 0x80;
            while (i < 128) buf_[i++] = 0x00;
            transform(buf_);
            std::memset(buf_, 0, 112);
        }
        for (int b = 0; b < 8; ++b) {
            buf_[119 - b] = static_cast<uint8_t>((total_hi >> (b * 8)) & 0xFFu);
            buf_[127 - b] = static_cast<uint8_t>((total_lo >> (b * 8)) & 0xFFu);
        }
        transform(buf_);
        for (int j = 0; j < 8; ++j) {
            for (int b = 0; b < 8; ++b) {
                out[j * 8 + b] = static_cast<uint8_t>((state_[j] >> (56 - b * 8)) & 0xFFu);
            }
        }
        return out;
    }
private:
    uint64_t state_[8], bitlen_lo_, bitlen_hi_;
    uint8_t buf_[128];
    size_t buflen_;
    static inline uint64_t rotr64(uint64_t x, uint64_t n) { return (x >> n) | (x << (64 - n)); }
    void transform(const uint8_t block[128]) {
        static const uint64_t k[80] = {
            0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL, 0xb5c0fbcfec4d3b2fULL, 0xe9b5dba58189dbbcULL,
            0x3956c25bf348b538ULL, 0x59f111f1b605d019ULL, 0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL,
            0xd807aa98a3030242ULL, 0x12835b0145706fbeULL, 0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL,
            0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL, 0x9bdc06a725c71235ULL, 0xc19bf174cf692694ULL,
            0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL, 0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL,
            0x2de92c6f592b0275ULL, 0x4a7484aa6ea6e483ULL, 0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL,
            0x983e5152ee66dfabULL, 0xa831c66d2db43210ULL, 0xb00327c898fb213fULL, 0xbf597fc7beef0ee4ULL,
            0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL, 0x06ca6351e003826fULL, 0x142929670a0e6e70ULL,
            0x27b70a8546d22ffcULL, 0x2e1b21385c26c926ULL, 0x4d2c6dfc5ac42aedULL, 0x53380d139d95b3dfULL,
            0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL, 0x81c2c92e47edaee6ULL, 0x92722c851482353bULL,
            0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL, 0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL,
            0xd192e819d6ef5218ULL, 0xd69906245565a910ULL, 0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL,
            0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL, 0x2748774cdf8eeb99ULL, 0x34b0bcb5e19b48a8ULL,
            0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL, 0x5b9cca4f7763e373ULL, 0x682e6ff3d6b2b8a3ULL,
            0x748f82ee5defb2fcULL, 0x78a5636f43172f60ULL, 0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
            0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL, 0xbef9a3f7b2c67915ULL, 0xc67178f2e372532bULL,
            0xca273eceea26619cULL, 0xd186b8c721c0c207ULL, 0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL,
            0x06f067aa72176fbaULL, 0x0a637dc5a2c898a6ULL, 0x113f9804bef90daeULL, 0x1b710b35131c471bULL,
            0x28db77f523047d84ULL, 0x32caab7b40c72493ULL, 0x3c9ebe0a15c9bebcULL, 0x431d67c49c100d4cULL,
            0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL, 0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL
        };
        uint64_t w[80];
        for (int i = 0; i < 16; ++i) {
            uint64_t v = 0;
            for (int b = 0; b < 8; ++b) v = (v << 8) | block[i * 8 + b];
            w[i] = v;
        }
        for (int i = 16; i < 80; ++i) {
            uint64_t s0 = rotr64(w[i - 15], 1) ^ rotr64(w[i - 15], 8) ^ (w[i - 15] >> 7);
            uint64_t s1 = rotr64(w[i - 2], 19) ^ rotr64(w[i - 2], 61) ^ (w[i - 2] >> 6);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint64_t a = state_[0], b = state_[1], c = state_[2], d = state_[3];
        uint64_t e = state_[4], f = state_[5], g = state_[6], h = state_[7];
        for (int i = 0; i < 80; ++i) {
            uint64_t S1 = rotr64(e, 14) ^ rotr64(e, 18) ^ rotr64(e, 41);
            uint64_t ch = (e & f) ^ ((~e) & g);
            uint64_t t1 = h + S1 + ch + k[i] + w[i];
            uint64_t S0 = rotr64(a, 28) ^ rotr64(a, 34) ^ rotr64(a, 39);
            uint64_t maj = (a & b) ^ (a & c) ^ (b & c);
            uint64_t t2 = S0 + maj;
            h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        state_[0] += a; state_[1] += b; state_[2] += c; state_[3] += d;
        state_[4] += e; state_[5] += f; state_[6] += g; state_[7] += h;
    }
};

namespace ed25519 {
using gf = int64_t[16];
static inline void set25519(gf r, const gf a) { for (int i = 0; i < 16; ++i) r[i] = a[i]; }
static inline void car25519(gf o) {
    for (int i = 0; i < 16; ++i) {
        o[i] += (1LL << 16);
        int64_t c = o[i] >> 16;
        o[(i + 1) * (i < 15)] += c - 1 + 37 * (c - 1) * (i == 15);
        o[i] -= c << 16;
    }
}
static inline void sel25519(gf p, gf q, int b) {
    int64_t c = ~(b - 1);
    for (int i = 0; i < 16; ++i) { int64_t t = c & (p[i] ^ q[i]); p[i] ^= t; q[i] ^= t; }
}
static inline void pack25519(uint8_t* o, const gf n) {
    gf m, t; set25519(t, n); car25519(t); car25519(t); car25519(t);
    for (int j = 0; j < 2; ++j) {
        m[0] = t[0] - 0xffed;
        for (int i = 1; i < 15; ++i) { m[i] = t[i] - 0xffff - ((m[i - 1] >> 16) & 1); m[i - 1] &= 0xffff; }
        m[15] = t[15] - 0x7fff - ((m[14] >> 16) & 1);
        int b = (m[15] >> 16) & 1; m[14] &= 0xffff; sel25519(t, m, 1 - b);
    }
    for (int i = 0; i < 16; ++i) { o[2 * i] = static_cast<uint8_t>(t[i] & 0xff); o[2 * i + 1] = static_cast<uint8_t>(t[i] >> 8); }
}
static inline void A(gf o, const gf a, const gf b) { for (int i = 0; i < 16; ++i) o[i] = a[i] + b[i]; }
static inline void Z(gf o, const gf a, const gf b) { for (int i = 0; i < 16; ++i) o[i] = a[i] - b[i]; }
static inline void M(gf o, const gf a, const gf b) {
    int64_t t[31] = {0};
    for (int i = 0; i < 16; ++i) for (int j = 0; j < 16; ++j) t[i + j] += a[i] * b[j];
    for (int i = 0; i < 15; ++i) t[i] += 38 * t[i + 16];
    for (int i = 0; i < 16; ++i) o[i] = t[i];
    car25519(o); car25519(o);
}
static inline void S(gf o, const gf a) { M(o, a, a); }
static inline void inv25519(gf o, const gf i) {
    gf c; set25519(c, i);
    for (int a = 253; a >= 0; --a) { S(c, c); if (a != 2 && a != 4) M(c, c, i); }
    set25519(o, c);
}
static inline void packPoint(uint8_t* r, gf p[4]) {
    gf tx, ty, zi; inv25519(zi, p[2]); M(tx, p[0], zi); M(ty, p[1], zi); pack25519(r, ty);
    uint8_t tx_bytes[32]; pack25519(tx_bytes, tx);
    r[31] = (r[31] & 0x7F) | static_cast<uint8_t>((tx_bytes[0] & 1) << 7);
}
static inline void addPoint(gf p[4], gf q[4]) {
    static const gf D2 = {0xf159,0x26b2,0x9b94,0xebd6,0xb156,0x8283,0x149a,0x00e0,0xd130,0xeef3,0x80f2,0x198e,0xfce7,0x56df,0xd9dc,0x2406};
    gf a, b, c, d, t, e, f, g, h;
    Z(a, p[1], p[0]); Z(t, q[1], q[0]); M(a, a, t); A(b, p[0], p[1]); A(t, q[0], q[1]); M(b, b, t);
    M(c, p[3], q[3]); M(c, c, D2); M(d, p[2], q[2]); A(d, d, d); Z(e, b, a); Z(f, d, c); A(g, d, c); A(h, b, a);
    M(p[0], e, f); M(p[1], h, g); M(p[2], g, f); M(p[3], e, h);
}
static inline void cswapPoint(gf p[4], gf q[4], uint8_t b) { for (int i = 0; i < 4; ++i) sel25519(p[i], q[i], b); }
static inline void scalarbase(gf p[4], const uint8_t* s) {
    static const gf X = {0xd51a,0x8f25,0x2d60,0xc956,0xa7b2,0x9525,0xc760,0x692c,0xdc5c,0xfdd6,0xe231,0xc0a4,0x53fe,0xcd6e,0x36d3,0x2169};
    static const gf Y = {0x6658,0x6666,0x6666,0x6666,0x6666,0x6666,0x6666,0x6666,0x6666,0x6666,0x6666,0x6666,0x6666,0x6666,0x6666,0x6666};
    gf q[4];
    for (int i = 0; i < 16; ++i) { p[0][i] = 0; p[1][i] = 0; p[2][i] = 0; p[3][i] = 0; q[2][i] = 0; }
    p[1][0] = 1; p[2][0] = 1; set25519(q[0], X); set25519(q[1], Y); q[2][0] = 1; M(q[3], X, Y);
    for (int i = 255; i >= 0; --i) {
        uint8_t b = (s[i / 8] >> (i & 7)) & 1;
        cswapPoint(p, q, b); addPoint(q, p); addPoint(p, p); cswapPoint(p, q, b);
    }
}
static inline void modL(uint8_t* r, int64_t x[64]) {
    static const int64_t L[32] = {0xed,0xd3,0xf5,0x5c,0x1a,0x63,0x12,0x58,0xd6,0x9c,0xf7,0xa2,0xde,0xf9,0xde,0x14,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0x10};
    for (int i = 63; i >= 32; --i) {
        int64_t carry = 0;
        for (int j = i - 32, k = i - 12; j < k; ++j) {
            x[j] += carry - 16 * x[i] * L[j - (i - 32)];
            carry = (x[j] + 128) >> 8; x[j] -= carry << 8;
        }
        x[i - 12] += carry; x[i] = 0;
    }
    int64_t carry = 0;
    for (int j = 0; j < 32; ++j) { x[j] += carry - (x[31] >> 4) * L[j]; carry = x[j] >> 8; x[j] &= 255; }
    for (int j = 0; j < 32; ++j) x[j] -= carry * L[j];
    for (int i = 0; i < 32; ++i) { x[i + 1] += x[i] >> 8; r[i] = static_cast<uint8_t>(x[i] & 255); }
}
static inline void reduce(uint8_t* r) {
    int64_t x[64];
    for (int i = 0; i < 64; ++i) { x[i] = r[i]; r[i] = 0; }
    modL(r, x);
}
inline void derivePublicKey(const uint8_t seed[32], uint8_t pk[32]) {
    SHA512 sha; sha.update(seed, 32); auto d = sha.digest();
    d[0] &= 248; d[31] &= 127; d[31] |= 64;
    gf p[4]; scalarbase(p, d.data()); packPoint(pk, p);
}
inline std::string signMessageHex(const uint8_t seed[32], const std::string& message) {
    SHA512 sha1; sha1.update(seed, 32); auto d = sha1.digest();
    d[0] &= 248; d[31] &= 127; d[31] |= 64;
    uint8_t pk[32]; gf p[4]; scalarbase(p, d.data()); packPoint(pk, p);
    SHA512 sha_r; sha_r.update(d.data() + 32, 32);
    sha_r.update(reinterpret_cast<const uint8_t*>(message.data()), message.size());
    auto r_full = sha_r.digest(); reduce(r_full.data());
    uint8_t R_bytes[32]; scalarbase(p, r_full.data()); packPoint(R_bytes, p);
    SHA512 sha_h; sha_h.update(R_bytes, 32); sha_h.update(pk, 32);
    sha_h.update(reinterpret_cast<const uint8_t*>(message.data()), message.size());
    auto h_full = sha_h.digest(); reduce(h_full.data());
    int64_t x[64] = {0};
    for (int i = 0; i < 32; ++i) x[i] = r_full[i];
    for (int i = 0; i < 32; ++i) for (int j = 0; j < 32; ++j) x[i + j] += static_cast<int64_t>(h_full[i]) * static_cast<int64_t>(d[j]);
    uint8_t S_bytes[32]; modL(S_bytes, x);
    uint8_t sig[64]; std::memcpy(sig, R_bytes, 32); std::memcpy(sig + 32, S_bytes, 32);
    return toHex(sig, 64);
}
} // namespace ed25519
#endif

inline std::string getSelfExecutablePath(const char* argv0) {
#ifdef _WIN32
    char buf[MAX_PATH];
    DWORD len = GetModuleFileNameA(nullptr, buf, MAX_PATH);
    if (len > 0 && len < MAX_PATH) {
        return std::string(buf, len);
    }
#else
    char buf[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len > 0) {
        buf[len] = '\0';
        return std::string(buf);
    }
#endif
    return argv0 ? std::string(argv0) : std::string();
}

inline std::string computeSelfBinarySha256(const char* argv0) {
    std::string path = getSelfExecutablePath(argv0);
    if (path.empty()) return "unknown";

    std::ifstream in(path, std::ios::binary);
    if (!in) return "unknown";

    SHA256 sha;
    char buffer[8192];
    while (in.read(buffer, sizeof(buffer)) || in.gcount() > 0) {
        sha.update(reinterpret_cast<const uint8_t*>(buffer), static_cast<size_t>(in.gcount()));
    }
    return SHA256::toHex(sha.digest());
}

inline std::string getProtocolAttestationSecret() {
    static const uint8_t enc[32] = {
        0x78, 0x41, 0x99, 0x12, 0x4E, 0x8C, 0x33, 0xD5,
        0xA1, 0x6F, 0x24, 0xE7, 0x5B, 0x09, 0xC8, 0x82,
        0x1D, 0xF3, 0x7A, 0xB4, 0x60, 0x2E, 0x95, 0x47,
        0xE0, 0x58, 0x3C, 0x8B, 0xD2, 0x16, 0xAF, 0x9E
    };
    uint8_t mask = 0x5A;
    uint8_t dec[32];
    for (int i = 0; i < 32; ++i) {
        dec[i] = static_cast<uint8_t>(enc[i] ^ (mask + i * 7));
    }
    return toHex(dec, 32);
}

} // namespace crypto
