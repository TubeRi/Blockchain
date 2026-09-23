// vuhash.cpp
// VUHash-256: a custom, educational (NON-cryptographic) hash function.
// This is NOT SHA-256, NOT MD5, and does not call any standard/library
// hash function - the mixing logic below is written from scratch for
// this assignment ("Sukurk savo maisos generatoriu").
//
// Usage:
//   vuhash <file>   -> hashes the exact bytes of <file>          (file mode)
//   vuhash          -> prompts for one line of text, hashes its
//                      UTF-8 bytes                                (manual mode)
//
// Design notes (kept simple on purpose - see README.md):
//  - Internal state = four independent 64-bit "lanes" => 256-bit digest.
//  - Absorb phase: every input byte is folded into one lane (round robin)
//    using rotate + xor + multiply-by-odd-constant + position counter.
//  - The exact byte length is folded in as well (so e.g. "a" and "a\0"
//    do not collapse to the same absorption pattern).
//  - Diffuse phase: a few rounds of cross-lane mixing so that, ideally,
//    every output bit depends on every input byte (avalanche effect -
//    this is *tested*, not proven, in the later experiments).
//
// Requirements this file addresses (checklist items 1-2, "2-3 skyriai"):
//  1. Own algorithm; supports file-content mode and manual-text mode;
//     reports errors instead of silently treating them as empty input.
//  2. UTF-8 input, exact bytes hashed (no trimming/case changes/line-ending
//     changes), fixed 256-bit output, hex output keeps leading zeros.
//  4. Deterministic: no clock, no RNG, no persistent state between calls.

#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

using u8  = std::uint8_t;
using u64 = std::uint64_t;

static inline u64 rotl64(u64 x, int r) {
    return (x << r) | (x >> (64 - r));
}

// ---------------------------------------------------------------------
// VUHash-256
// ---------------------------------------------------------------------
struct VUHash256 {
    u64 lane[4];

    // Fixed initial constants. Arbitrary odd-ish 64-bit values chosen by
    // hand for this assignment - NOT taken from SHA-256/MD5/etc.
    static constexpr u64 IV[4] = {
        0x1234567890ABCDEFULL,
        0xFEDCBA0987654321ULL,
        0x0F1E2D3C4B5A6978ULL,
        0x8796A5B4C3D2E1F0ULL
    };
    // Odd multiplicative constants, one per lane (odd => multiplication
    // mod 2^64 stays invertible, giving better mixing).
    static constexpr u64 PRIME[4] = {
        0x9E3779B97F4A7C15ULL,
        0xC2B2AE3D27D4EB4FULL,
        0x165667B19E3779F9ULL,
        0x27D4EB2F165667C5ULL
    };

    VUHash256() {
        lane[0] = IV[0]; lane[1] = IV[1]; lane[2] = IV[2]; lane[3] = IV[3];
    }

    void absorb(const u8* data, std::size_t len) {
        for (std::size_t i = 0; i < len; ++i) {
            int L = static_cast<int>(i & 3u); // i mod 4 -> which lane
            lane[L] ^= static_cast<u64>(data[i]);
            lane[L]  = rotl64(lane[L], 5);
            lane[L] *= PRIME[L];
            lane[L] += static_cast<u64>(i) + 1;
        }
        // Fold the exact byte length into the state (basic domain
        // separation between inputs of different lengths).
        lane[0] ^= rotl64(static_cast<u64>(len), 7);
        lane[2] += static_cast<u64>(len) * PRIME[3];
    }

    void diffuse() {
        for (int round = 0; round < 6; ++round) {
            lane[0] ^= rotl64(lane[1], 13);
            lane[1] += lane[2];
            lane[2] ^= rotl64(lane[3], 17);
            lane[3] += lane[0];

            lane[1] = rotl64(lane[1], 29);
            lane[3] = rotl64(lane[3], 31);

            std::swap(lane[0], lane[2]); // extra cross-mixing each round
        }
    }

    // 32 raw digest bytes, big-endian within each lane.
    std::vector<u8> digestBytes() {
        diffuse();
        std::vector<u8> out(32);
        for (int L = 0; L < 4; ++L)
            for (int b = 0; b < 8; ++b)
                out[L * 8 + b] = static_cast<u8>(lane[L] >> (8 * (7 - b)));
        return out;
    }
};

constexpr u64 VUHash256::IV[4];
constexpr u64 VUHash256::PRIME[4];

std::string toHex(const std::vector<u8>& bytes) {
    static const char* hexchars = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (u8 b : bytes) {
        out.push_back(hexchars[b >> 4]);
        out.push_back(hexchars[b & 0x0F]);
    }
    return out; // always 64 hex chars for a 32-byte digest; leading zeros kept
}

std::string hashBytes(const u8* data, std::size_t len) {
    VUHash256 h;
    h.absorb(data, len);
    return toHex(h.digestBytes());
}

// Reads a whole file as raw bytes (binary mode = no newline translation).
bool readFileBytes(const std::string& path, std::vector<u8>& out, std::string& errMsg) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        errMsg = "Cannot open file: " + path;
        return false;
    }
    f.seekg(0, std::ios::end);
    std::streamoff size = f.tellg();
    if (size < 0) {
        errMsg = "Cannot determine size of file: " + path;
        return false;
    }
    f.seekg(0, std::ios::beg);
    out.resize(static_cast<std::size_t>(size));
    if (size > 0 && !f.read(reinterpret_cast<char*>(out.data()), size)) {
        errMsg = "Error while reading file: " + path;
        return false;
    }
    return true;
}

int main(int argc, char** argv) {
    if (argc > 2) {
        std::cerr << "Usage: " << argv[0] << " [file]\n";
        return 1;
    }

    if (argc == 2) {
        // ---------------- File mode ----------------
        std::string path = argv[1];
        std::vector<u8> data;
        std::string err;
        if (!readFileBytes(path, data, err)) {
            std::cerr << "ERROR: " << err << "\n";
            return 1; // report the error, do NOT silently hash as empty
        }
        std::cout << "Mode: file\n";
        std::cout << "File: " << path << "\n";
        std::cout << "Bytes hashed: " << data.size() << "\n";
        std::cout << "VUHash-256: " << hashBytes(data.data(), data.size()) << "\n";
        return 0;
    } else {
        // ---------------- Manual input mode ----------------
        std::cout << "Mode: manual input\n";
        std::cout << "Enter text to hash (the Enter keystroke itself is NOT "
                     "included in the hashed bytes): ";
        std::string line;
        if (!std::getline(std::cin, line)) {
            std::cerr << "ERROR: no input received\n";
            return 1;
        }
        const u8* data = reinterpret_cast<const u8*>(line.data());
        std::cout << "Bytes hashed: " << line.size()
                  << " (UTF-8 bytes of the entered line, no trailing newline)\n";
        std::cout << "VUHash-256: " << hashBytes(data, line.size()) << "\n";
        return 0;
    }
}