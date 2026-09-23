// simple_hash256.cpp - beginner-friendly 256-bit hash (4 lanes of FNV-1a)
#include <cstdint>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>
#include <iomanip>

// 4 independent 64-bit "lanes". Each starts differently and mixes
// with a different prime, so they don't all produce the same pattern.
const uint64_t OFFSET[4] = {
    14695981039346656037ULL, 9506415940880907919ULL,
    2748302926561850637ULL,  1181784098128809349ULL
};
const uint64_t PRIME[4] = {
    1099511628211ULL, 1099511628223ULL,
    1099511628233ULL, 1099511628251ULL
};

// Runs all 4 lanes over the same input bytes at once.
// Same bytes in -> same 4 numbers out, every time.
void hashBytes(const std::vector<unsigned char>& data, uint64_t lanes[4]) {
    for (int i = 0; i < 4; i++) lanes[i] = OFFSET[i];

    for (unsigned char byte : data) {
        for (int i = 0; i < 4; i++) {
            lanes[i] ^= byte;    // mix the byte in
            lanes[i] *= PRIME[i]; // spread the bits around
        }   // so called shit avalanche effect but not secured enough
    }
}

// Turns 4 numbers into one 64-character hex string, keeping leading zeros.
std::string toHex(const uint64_t lanes[4]) {
    std::ostringstream out;
    for (int i = 0; i < 4; i++) {
        out << std::hex << std::setfill('0') << std::setw(16) << lanes[i];
    }
    return out.str();
}

int main(int argc, char* argv[]) {
    std::vector<unsigned char> data;

    if (argc >= 2) {
        // Read from a file, exactly as-is (binary mode).
        std::ifstream file(argv[1], std::ios::binary);
        if (!file) {
            std::cerr << "Could not open file: " << argv[1] << "\n";
            return 1;
        }
        char c;
        while (file.get(c)) data.push_back(static_cast<unsigned char>(c));
    } else {
        // Read from stdin, exactly as-is.
        char c;
        while (std::cin.get(c)) data.push_back(static_cast<unsigned char>(c));
    }

    if (data.size() > 100 * 1024 * 1024) { // 100 MB limit
        std::cerr << "Input too large.\n";
        return 1;
    }

    uint64_t lanes[4];
    hashBytes(data, lanes);
    std::cout << toHex(lanes) << "\n";
    return 0;
}