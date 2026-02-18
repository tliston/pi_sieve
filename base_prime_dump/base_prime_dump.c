#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <emmintrin.h> // For SSE2 intrinsics

// We're going to size our segment block to fit within the L1 cache
// Additionally, we must use a multiple of 24 to preserve the use of
// of our "mod 3" pattern...
// Standard L1 cache: 32KB = 32768. Multiple of 24 below that is 32760.
static const char szNoMem[] = "Error: Unable to allocate memory\n";

// Pre-computed masks
static uint8_t mask[] = { 1, 2, 4, 8, 16, 32, 64, 128 };
static uint8_t mask2[] = { 254, 253, 251, 247, 239, 223, 191, 127 };

// --- Helper: Simple Sieve to find base primes up to sqrt(max_num) ---
// This uses the logic from my primes_new code, scaled down just for the square root
uint64_t* GetBasePrimes(uint64_t limit, size_t *count) {
    uint64_t byte_len = (limit >> 4) + 1;
    uint8_t *mem = (uint8_t *)malloc(byte_len);
    memset(mem, 0xFF, byte_len); // Naive init is fine for small root

    // Clear 0 and 1 bits (i.e. 1 and 3)
    mem[0] &= 0xFE; 

    uint64_t i_limit = sqrt(limit);
    for (uint64_t i = 3; i <= i_limit; i += 2) {
        if (mem[i >> 4] & mask[(i >> 1) & 7]) {
            uint64_t start = (i * i) >> 1;
            for (uint64_t j = start; j < (limit >> 1); j += i) {
                mem[j >> 3] &= mask2[j & 7];
            }
        }
    }
    // Count primes to allocate array
    size_t c = 0;
    // Start at 3 because 1 is not prime, and 2 is handled separately
    for (uint64_t i = 3; i <= limit; i += 2) {
        if (mem[i >> 4] & mask[(i >> 1) & 7]) c++;
    }
    uint64_t *primes = malloc(sizeof(uint64_t)*c);
    size_t idx = 0;
    for (uint64_t i = 3; i <= limit; i += 2) {
        if (mem[i >> 4] & mask[(i >> 1) & 7]) {
            primes[idx] = i;
            idx++;
        }
    }    
    *count = idx;
    free(mem);
    return primes;
}

int main(int argc, char *argv[]) {
    uint64_t *primes;
    FILE *fp = NULL;

    uint64_t max_num = 18446744073709551615ULL;
    if ((max_num & 1) == 0) max_num--; // Ensure odd max
    size_t prime_count = 0;
    if((primes = (uint64_t *)GetBasePrimes(sqrt(max_num), &prime_count)) == NULL) {
        printf(szNoMem);
        exit(1);
    }
    if ((fp = fopen("base_primes.bin", "wb"))) {
        fwrite(primes, sizeof(uint64_t), prime_count, fp);
        fclose(fp);
    }    
    free(primes);
    return EXIT_SUCCESS;
}