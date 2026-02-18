#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include <unistd.h>
#include <locale.h>
#include <pthread.h>

// --- ARCHITECTURE DETECTION ---
#if defined(__x86_64__) || defined(_M_X64)
    #include <emmintrin.h>
    #define SIMD_X64
#elif defined(__aarch64__) || defined(_M_ARM64)
    #include <arm_neon.h>
    #define SIMD_NEON
#endif

#define BLOCK_BYTES             32760
#define INPUT_BUFFER_SIZE       10000
#define BLOCK_BITS              (BLOCK_BYTES * 8)
#ifdef RELEASE
#define STATE_FILE              "/pi-sieve/sieve_progress.bin"
#define PRIME_DUMP              "/pi-sieve/base_primes.bin" 
#else
#define STATE_FILE              "sieve_progress.bin"
#define PRIME_DUMP              "base_primes.bin" 
#endif
#define SAVE_INTERVAL_SECS      60

// --- ANSI TERMINAL MACROS ---
#define TERM_CLEAR        "\033[2J"
#define TERM_HOME         "\033[H"
#define TERM_HIDE_CURSOR  "\033[?25l"
#define TERM_SHOW_CURSOR  "\033[?25h"
#define TERM_SET_SCROLL   "\033[6;r"  // Scroll region: Line 6 to Bottom
#define TERM_RESET_SCROLL "\033[r"    // Reset scroll region to full screen
#define TERM_SAVE_CURSOR  "\0337"     // Save cursor position
#define TERM_RESTORE_CURSOR "\0338"   // Restore cursor position

typedef struct {
    uint64_t p;
    uint64_t next_bit;
} PrimeState;

typedef struct {
    uint64_t cursor;
    uint64_t count;
    time_t saved_rt;
} SavedState;

// Sync Globals
pthread_mutex_t buffer_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  buffer_ready = PTHREAD_COND_INITIALIZER;
pthread_cond_t  buffer_empty = PTHREAD_COND_INITIALIZER;

uint8_t *buffers[2];
uint64_t buffer_cursors[2];
int buffer_status[2] = {0, 0}; // 0 = empty/processing, 1 = ready for print
int current_sieve_idx = 0;
int current_print_idx = 0;

volatile sig_atomic_t stop_flag = 0;
static uint8_t mask2[] = { 254, 253, 251, 247, 239, 223, 191, 127 };
PrimeState *primes;
size_t prime_count;
uint64_t global_cursor = 0;
uint64_t session_primes = 0; // Count primes found in this run

void handle_sigint(int sig) { 
    stop_flag = 1; 
    // Wake up threads if they are waiting
    pthread_cond_broadcast(&buffer_ready);
    pthread_cond_broadcast(&buffer_empty);
}

void restore_terminal() {
    printf(TERM_RESET_SCROLL);
    printf(TERM_SHOW_CURSOR);
    fflush(stdout);
}

uint64_t calculate_next_bit(uint64_t p, uint64_t global_offset) {
    uint64_t start_index = (p * p) >> 1;
    if (start_index < global_offset) {
        uint64_t diff = global_offset - start_index;
        uint64_t k = (diff + p - 1) / p; 
        return start_index + (k * p);
    }
    return start_index;
}

// --- Background Sieve Thread ---
void* sieve_thread_func(void* arg) {
    uint8_t pattern_a[16] = { 0x6D, 0xDB, 0xB6, 0x6D, 0xDB, 0xB6, 0x6D, 0xDB, 
                              0xB6, 0x6D, 0xDB, 0xB6, 0x6D, 0xDB, 0xB6, 0x6D };
    uint8_t pattern_b[8]  = { 0xDB, 0xB6, 0x6D, 0xDB, 0xB6, 0x6D, 0xDB, 0xB6 };
#ifdef SIMD_X64
    __m128i v16 = _mm_loadu_si128((__m128i *)pattern_a);
    __m128i v8  = _mm_loadl_epi64((__m128i *)pattern_b);
#elif defined(SIMD_NEON)
    uint8x16_t v16 = vld1q_u8(pattern_a);
    uint8x8_t  v8  = vld1_u8(pattern_b);
#endif
    while (!stop_flag) {
        pthread_mutex_lock(&buffer_mutex);
        while (buffer_status[current_sieve_idx] == 1 && !stop_flag)
            pthread_cond_wait(&buffer_empty, &buffer_mutex);
        if (stop_flag) { pthread_mutex_unlock(&buffer_mutex); break; }
        uint8_t *block = buffers[current_sieve_idx];
        uint64_t cursor = global_cursor;
        buffer_cursors[current_sieve_idx] = cursor;
        pthread_mutex_unlock(&buffer_mutex);
        // --- MATH WORK START ---
        uint8_t *p2 = block;
        while (p2 < block + BLOCK_BYTES) {
#ifdef SIMD_X64
            _mm_storeu_si128((__m128i *)p2, v16);
            _mm_storel_epi64((__m128i *)(p2 + 16), v8);
#elif defined(SIMD_NEON)
            vst1q_u8(p2, v16);
            vst1_u8(p2 + 16, v8);
#else
            memcpy(p2, pattern_a, 16); memcpy(p2+16, pattern_b, 8);
#endif
            p2 += 24;
        }
        if (cursor == 0) block[0] = 0x6E;
        for (size_t i = 0; i < prime_count; i++) {
            uint64_t p = primes[i].p;
            uint64_t next = primes[i].next_bit;
            if (next >= cursor + BLOCK_BITS) continue;
            uint64_t chk_bit = next - cursor;
            uint64_t step = p;
            if (BLOCK_BITS > (step << 3)) {
                uint64_t unroll_limit = BLOCK_BITS - (step << 3);
                while (chk_bit < unroll_limit) {
                    block[chk_bit >> 3] &= mask2[chk_bit & 7];
                    block[(chk_bit + step) >> 3] &= mask2[(chk_bit + step) & 7];
                    block[(chk_bit + 2*step) >> 3] &= mask2[(chk_bit + 2*step) & 7];
                    block[(chk_bit + 3*step) >> 3] &= mask2[(chk_bit + 3*step) & 7];
                    block[(chk_bit + 4*step) >> 3] &= mask2[(chk_bit + 4*step) & 7];
                    block[(chk_bit + 5*step) >> 3] &= mask2[(chk_bit + 5*step) & 7];
                    block[(chk_bit + 6*step) >> 3] &= mask2[(chk_bit + 6*step) & 7];
                    block[(chk_bit + 7*step) >> 3] &= mask2[(chk_bit + 7*step) & 7];
                    chk_bit += (step << 3);
                }
            }
            while (chk_bit < BLOCK_BITS) {
                block[chk_bit >> 3] &= mask2[chk_bit & 7];
                chk_bit += step;
            }
            primes[i].next_bit = cursor + chk_bit;
        }
        // --- MATH WORK END ---
        pthread_mutex_lock(&buffer_mutex);
        buffer_status[current_sieve_idx] = 1;
        global_cursor += BLOCK_BITS;
        current_sieve_idx = 1 - current_sieve_idx;
        pthread_cond_signal(&buffer_ready);
        pthread_mutex_unlock(&buffer_mutex);
    }
    global_cursor -= BLOCK_BITS;    
    return NULL;
}

int main(int argc, char *argv[]) {
    SavedState ss;
    time_t initial_rt = 0;
    signal(SIGINT, handle_sigint);
    setlocale(LC_ALL, "");
    char buf1[1024];
    int padding = 0;
    // Load primes
    FILE *pf = fopen(PRIME_DUMP, "rb");
    if (!pf) { perror("Prime dump file error."); return 1; }
    fseek(pf, 0, SEEK_END);
    prime_count = ftell(pf) / sizeof(uint64_t);
    fseek(pf, 0, SEEK_SET);
    primes = malloc(sizeof(PrimeState) * prime_count);
    uint64_t *p_val_buf = malloc(sizeof(uint64_t) * INPUT_BUFFER_SIZE);
    if ((p_val_buf == NULL) | (primes == NULL)) { perror("Memory allocation error.\n"); return 1; }
    size_t i = 0;
    while(i < prime_count) {
        size_t read_count = fread(p_val_buf, sizeof(uint64_t), INPUT_BUFFER_SIZE, pf);
        if(read_count) for(size_t j = 0; j < read_count; j++) { primes[i].p = p_val_buf[j]; i++; }
    }
    fclose(pf);
    free(p_val_buf);
    // Load state
    if(argc == 1) {
        FILE *sf = fopen(STATE_FILE, "rb");
        if (sf) { 
            fread(&ss, sizeof(SavedState), 1, sf); 
            fclose(sf); 
            global_cursor = ss.cursor;
            session_primes = ss.count;
            initial_rt = ss.saved_rt;
        }
    }
    for (size_t i = 0; i < prime_count; i++) primes[i].next_bit = calculate_next_bit(primes[i].p, global_cursor);
    // Prepare Buffers
    posix_memalign((void**)&buffers[0], 16, BLOCK_BYTES);
    posix_memalign((void**)&buffers[1], 16, BLOCK_BYTES);
    // Start Sieve Thread
    pthread_t sieve_tid;
    pthread_create(&sieve_tid, NULL, sieve_thread_func, NULL);
    time_t last_save = time(NULL);
    time_t start_time = last_save, current_rt = 0, remaining_rt;
    // --- SETUP TERMINAL ---
    printf(TERM_CLEAR);        // Clear Screen
    printf(TERM_SET_SCROLL);   // Set Scroll Region (Line 6-End)
    printf(TERM_HIDE_CURSOR);  // Hide Cursor
    printf(TERM_HOME);         // Go Home
    fflush(stdout);
    if (global_cursor == 0)
        session_primes++;
    // --- Main Printer Loop ---
    while (!stop_flag) {
        pthread_mutex_lock(&buffer_mutex);
        while (buffer_status[current_print_idx] == 0 && !stop_flag)
            pthread_cond_wait(&buffer_ready, &buffer_mutex);
        if (stop_flag) { pthread_mutex_unlock(&buffer_mutex); break; }
        uint8_t *block = buffers[current_print_idx];
        uint64_t cursor = buffer_cursors[current_print_idx];
        pthread_mutex_unlock(&buffer_mutex);
        // Printing (Consumer)
        for (uint64_t bit = 0; bit < BLOCK_BITS; bit++) {
            if (block[bit >> 3] & (1 << (bit & 7))) {
                uint64_t val = ((cursor + bit) * 2) + 1;
                if (val > 1) {
                    // Update Header every 255 primes (to save bandwidth)
                    if ((session_primes & 0xFF) == 0) {
                        current_rt = initial_rt + (time(NULL) - start_time);
                        printf(TERM_SAVE_CURSOR);
                        printf(TERM_SET_SCROLL);   // Set Scroll Region (Lines 3-End)
                        printf(TERM_HIDE_CURSOR);  // Hide Cursor
                        printf(TERM_HOME);         // Go Home
                        // Print Static Header Background (Optional)
                        printf("\x1b[30;44m");   // Black on Blue background
                        printf("           Pi Sieve - High Speed Prime Generation           \033[0m\n");
                        printf("\033[2;1H"); // Move to Line 2
                        printf("\033[K");    // Clear Line 2
                        snprintf(buf1, 1024, "Primes: \033[1;32m%'lu\033[0m", session_primes);
                        printf("%*s%s",(int)(71 - strlen(buf1))/2,"", buf1);
                        printf("\033[3;1H"); // Move to Line 3
                        printf("\033[K");    // Clear Line 3
                        double speed = ((double)val / current_rt);
                        remaining_rt = (time_t)(UINT64_MAX / speed) - current_rt;
                        snprintf(buf1, 1024, "Runtime: %'lu years %lu days %02lu:%02lu:%02lu", (current_rt / 31536000), (current_rt / 86400) % 365, (current_rt/3600) % 24, (current_rt / 60) % 60, current_rt % 60);
                        printf("%*s%s",(int)(60 - strlen(buf1))/2,"", buf1);
                        printf("\033[4;1H"); // Move to Line 4
                        printf("\033[K");    // Clear Line 4
                        snprintf(buf1, 1024, "Remaining: %'lu years %3lu days %02lu:%02lu:%02lu", (remaining_rt / 31536000), (remaining_rt / 86400) % 365, (remaining_rt/3600) % 24, (remaining_rt / 60) % 60, remaining_rt % 60);
                        printf("%*s%s",(int)(60 - strlen(buf1))/2,"", buf1);
                        printf("\033[5;1H"); // Move to Line 5
                        printf("\033[K");    // Clear Line 5
                        snprintf(buf1, 1024, "Speed: ~%'u,000 numbers/second", (uint)(speed/1000));
                        printf("%*s%s",(int)(60 - strlen(buf1))/2,"", buf1);
                        snprintf(buf1, 1024, "%'lu", val);
                        padding = (60 - strlen(buf1))/2;
                        printf(TERM_RESTORE_CURSOR);
                    }
                    printf("%*s%'lu \n", padding, "", val);
                    session_primes++;
                    usleep(10);
                }
            }
        }
        pthread_mutex_lock(&buffer_mutex);
        buffer_status[current_print_idx] = 0;
        current_print_idx = 1 - current_print_idx;
        pthread_cond_signal(&buffer_empty);        
        // Save periodically
        if (time(NULL) - last_save > SAVE_INTERVAL_SECS) {
            ss.count = session_primes;
            ss.cursor = cursor;
            ss.saved_rt = current_rt;
            FILE *temp_sf = fopen(STATE_FILE, "wb");
            if (temp_sf) { fwrite(&ss, sizeof(SavedState), 1, temp_sf); fclose(temp_sf); }
            last_save = time(NULL);
        }
        pthread_mutex_unlock(&buffer_mutex);
    }
    pthread_join(sieve_tid, NULL);
    // Final save
    FILE *final_sf = fopen(STATE_FILE, "wb");
    ss.count = session_primes;
    ss.cursor = global_cursor;
    ss.saved_rt = current_rt;
    if (final_sf) { fwrite(&ss, sizeof(SavedState), 1, final_sf); fclose(final_sf); }   
    restore_terminal(); // Restore terminal settings   
    printf("\n\n\n\033[K");
    free(primes); free(buffers[0]); free(buffers[1]);
    return 0;
}