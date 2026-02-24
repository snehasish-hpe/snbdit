//# Write pattern to file
//./snb_dit /tmp/testfile.bin 4096 write 0xDEADBEEF

//# Read and verify
//./snb_dit /tmp/testfile.bin 4096 read 0xDEADBEEF

//# Write + Read + Verify in one shot
//./snb_dit /tmp/testfile.bin 4096 readwrite 0xDEADBEEF

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>

#define ALIGNMENT   512              /* O_DIRECT requires 512-byte aligned buffers */
#define MB          (1024*1024)      /* 1 Megabyte */
#define CHUNK_SIZE  (4 * 1024 * 1024) /* 4 MB reusable chunk buffer */

/* Structure to hold the hex pattern tightly packed */
typedef struct __attribute__((packed)) {
    uint8_t  pattern8;
    uint16_t pattern16;
    uint32_t pattern32;
    uint64_t pattern64;
} HexPattern;

/* Parse hex string like "0xDEADBEEF" or "DEADBEEF" into uint64_t */
static uint64_t parse_hex(const char *hex_str) {
    uint64_t value = 0;
    if (strncmp(hex_str, "0x", 2) == 0 || strncmp(hex_str, "0X", 2) == 0) {
        hex_str += 2;
    }
    char *endptr;
    value = strtoull(hex_str, &endptr, 16);
    if (*endptr != '\0') {
        fprintf(stderr, "Invalid hex pattern: %s\n", hex_str);
        exit(EXIT_FAILURE);
    }
    return value;
}

/* Fill buffer with HexPattern structure tightly packed */
static void fill_buffer(uint8_t *buf, size_t size, const HexPattern *pat) {
    size_t pat_size = sizeof(HexPattern);
    size_t offset = 0;
    while (offset + pat_size <= size) {
        memcpy(buf + offset, pat, pat_size);
        offset += pat_size;
    }
    /* Fill remaining bytes with pattern8 */
    while (offset < size) {
        buf[offset++] = pat->pattern8;
    }
}

/* Print first N bytes of buffer as hex */
static void dump_hex(const uint8_t *buf, size_t len, const char *label) {
    printf("%s (first %zu bytes):\n  ", label, len > 32 ? 32 : len);
    size_t show = len > 32 ? 32 : len;
    for (size_t i = 0; i < show; i++) {
        printf("%02X ", buf[i]);
        if ((i + 1) % 16 == 0) printf("\n  ");
    }
    printf("\n");
}

/* Get current time in seconds as double */
static double get_time_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

/* Print progress bar in MB */
static void print_progress(const char *op, size_t done, size_t total) {
    double done_mb  = (double)done  / MB;
    double total_mb = (double)total / MB;
    int bar_width   = 40;
    int filled      = (int)((done_mb / total_mb) * bar_width);

    printf("\r[%s] [", op);
    for (int i = 0; i < bar_width; i++)
        printf(i < filled ? "#" : "-");
    printf("] %.2f / %.2f MB", done_mb, total_mb);
    fflush(stdout);
}

int main(int argc, char *argv[]) {
    if (argc != 5) {
        fprintf(stderr,
            "Usage: %s <filename> <size> <read|write|readwrite> <hex_pattern>\n"
            "  filename    : target file path\n"
            "  size        : number of bytes (e.g. 4096)\n"
            "  mode        : read | write | readwrite\n"
            "  hex_pattern : hex value e.g. 0xDEADBEEF\n",
            argv[0]);
        return EXIT_FAILURE;
    }

    const char *filename  = argv[1];
    size_t       size     = (size_t)strtoull(argv[2], NULL, 10);
    const char  *mode     = argv[3];
    uint64_t     hex_val  = parse_hex(argv[4]);

    /* Ensure size is a multiple of ALIGNMENT for O_DIRECT */
    if (size % ALIGNMENT != 0) {
        fprintf(stderr, "Size must be a multiple of %d for O_DIRECT\n", ALIGNMENT);
        return EXIT_FAILURE;
    }

    /* Build the packed HexPattern from parsed hex value */
    HexPattern pat;
    pat.pattern8  = (uint8_t) (hex_val & 0xFF);
    pat.pattern16 = (uint16_t)(hex_val & 0xFFFF);
    pat.pattern32 = (uint32_t)(hex_val & 0xFFFFFFFF);
    pat.pattern64 = hex_val;

    printf("=== Direct I/O Pattern Test ===\n");
    printf("File    : %s\n", filename);
    printf("Size    : %zu bytes (%.2f MB)\n", size, (double)size / MB);
    printf("Mode    : %s\n", mode);
    printf("Pattern : 0x%llX\n", (unsigned long long)hex_val);
    printf("Pattern structure (packed, %zu bytes):\n", sizeof(HexPattern));
    printf("  pattern8  = 0x%02X\n", pat.pattern8);
    printf("  pattern16 = 0x%04X\n", pat.pattern16);
    printf("  pattern32 = 0x%08X\n", pat.pattern32);
    printf("  pattern64 = 0x%016llX\n", (unsigned long long)pat.pattern64);
    printf("Buffer  : %d MB (reusable chunk)\n\n", CHUNK_SIZE / MB);

    /* Allocate a single reusable aligned chunk buffer */
    uint8_t *chunk_buf = NULL;
    if (posix_memalign((void **)&chunk_buf, ALIGNMENT, CHUNK_SIZE) != 0) {
        perror("posix_memalign");
        return EXIT_FAILURE;
    }

    /* Pre-fill the chunk buffer once with the pattern */
    fill_buffer(chunk_buf, CHUNK_SIZE, &pat);
    dump_hex(chunk_buf, CHUNK_SIZE, "Pattern buffer");

    /* ---- WRITE ---- */
    if (strcmp(mode, "write") == 0 || strcmp(mode, "readwrite") == 0) {
        int fd = open(filename, O_WRONLY | O_CREAT | O_DIRECT | O_TRUNC, 0644);
        if (fd < 0) {
            perror("open (write)");
            free(chunk_buf);
            return EXIT_FAILURE;
        }

        size_t  total_written = 0;
        double  t_start       = get_time_sec();

        while (total_written < size) {
            /* Use remaining size if less than CHUNK_SIZE */
            size_t  chunk    = (size - total_written) < (size_t)CHUNK_SIZE
                               ? (size - total_written) : (size_t)CHUNK_SIZE;
            ssize_t written  = pwrite(fd, chunk_buf, chunk, (off_t)total_written);
            if (written < 0) {
                perror("\npwrite");
                close(fd); free(chunk_buf);
                return EXIT_FAILURE;
            }
            total_written += (size_t)written;
            print_progress("WRITE", total_written, size);
        }

        double t_elapsed  = get_time_sec() - t_start;
        double throughput = ((double)total_written / MB) / t_elapsed;
        printf("\n[WRITE] Written %.2f MB in %.3f sec => %.2f MB/s\n",
               (double)total_written / MB, t_elapsed, throughput);
        close(fd);
    }

    /* ---- READ + VERIFY ---- */
    if (strcmp(mode, "read") == 0 || strcmp(mode, "readwrite") == 0) {

        /* Allocate a separate read chunk - same size as write chunk */
        uint8_t *read_buf = NULL;
        if (posix_memalign((void **)&read_buf, ALIGNMENT, CHUNK_SIZE) != 0) {
            perror("posix_memalign (read)");
            free(chunk_buf);
            return EXIT_FAILURE;
        }

        int fd = open(filename, O_RDONLY | O_DIRECT);
        if (fd < 0) {
            perror("open (read)");
            free(chunk_buf); free(read_buf);
            return EXIT_FAILURE;
        }

        size_t  total_read = 0;
        int     mismatch   = 0;
        double  t_start    = get_time_sec();

        while (total_read < size) {
            size_t  chunk      = (size - total_read) < (size_t)CHUNK_SIZE
                                 ? (size - total_read) : (size_t)CHUNK_SIZE;
            ssize_t bytes_read = pread(fd, read_buf, chunk, (off_t)total_read);
            if (bytes_read < 0) {
                perror("\npread");
                close(fd); free(chunk_buf); free(read_buf);
                return EXIT_FAILURE;
            }
            if (bytes_read == 0) break; /* EOF */

            total_read += (size_t)bytes_read;
            print_progress("READ ", total_read, size);

            /* Verify this chunk inline against re-filled pattern chunk */
            fill_buffer(chunk_buf, (size_t)bytes_read, &pat);
            for (ssize_t i = 0; i < bytes_read; i++) {
                if (chunk_buf[i] != read_buf[i]) {
                    fprintf(stderr,
                        "\n  MISMATCH at offset %zu (%.2f MB): "
                        "expected 0x%02X got 0x%02X\n",
                        (total_read - (size_t)bytes_read) + (size_t)i,
                        (double)((total_read - (size_t)bytes_read) + (size_t)i) / MB,
                        chunk_buf[i], read_buf[i]);
                    mismatch++;
                    if (mismatch >= 10) {
                        fprintf(stderr, "  ... (too many mismatches, stopping)\n");
                        goto verify_done;
                    }
                }
            }
        }

verify_done:
        ;
        double t_elapsed  = get_time_sec() - t_start;
        double throughput = ((double)total_read / MB) / t_elapsed;
        printf("\n[READ]  Read %.2f MB in %.3f sec => %.2f MB/s\n",
               (double)total_read / MB, t_elapsed, throughput);

        if (mismatch == 0)
            printf("[VERIFY] PASSED - All %.2f MB match the pattern!\n",
                   (double)total_read / MB);
        else
            printf("[VERIFY] FAILED - %d mismatch(es) found!\n", mismatch);

        close(fd);
        free(read_buf);
    }

    free(chunk_buf);
    return EXIT_SUCCESS;
}