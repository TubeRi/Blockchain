Custom Hash Function

The idea

A hash function turns any input (text or a file) into a fixed-size "fingerprint." Same input always gives the same fingerprint, but you can't reverse it back to the original

Two things matter for a hash to be good:

Deterministic — same input, same output, every time, no randomness or clock involved.
Avalanche effect — changing even one letter of the input should change the output completely.

How it works

Based on FNV-1a, run 4 times in parallel ("lanes") to get a 256-bit output instead of the usual 64-bit one.

Each lane starts with its own fixed number and its own prime.
For every byte of input, each lane does:
cpp
   lanes[i] ^= byte;     // mix the byte in
   lanes[i] *= PRIME[i]; // spread the bits around
Once all bytes are processed, the 4 final numbers are converted to hex (16 characters each, leading zeros kept) → 64 hex characters total = 256 bits.

Known limitation: the avalanche effect is weaker for bytes near the end of the input, since the last byte only gets mixed once before the loop ends. Kept it this way on purpose for simplicity.


How to run it
bash
# Compile
g++ -std=c++17 -Wall -o simple_hash256 simple_hash256.cpp

# Run on text
echo -n "hello world" | ./simple_hash256

# Run on a file
./simple_hash256 myfile.txt

Always 64 hex characters, no matter the input length.