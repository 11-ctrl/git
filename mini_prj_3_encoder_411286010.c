// mini_prj_3_encoder_fixed.c
//test git 002

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <inttypes.h>

#define MAX_SYMBOLS 256

// ---------- CSV Safe Symbol Representation ----------
static void symbol_to_csv_repr(unsigned char c, char *out, size_t outsz) {
    if (c == '\"') { snprintf(out, outsz, "\"\""); return; } // CSV quote escape
    if (c == '\n') { snprintf(out, outsz, "\\n"); return; }
    if (c == '\r') { snprintf(out, outsz, "\\r"); return; }
    if (c == '\t') { snprintf(out, outsz, "\\t"); return; }
    if (c == '\\') { snprintf(out, outsz, "\\\\"); return; }
    if (c >= 32 && c <= 126) snprintf(out, outsz, "%c", c);
    else snprintf(out, outsz, "\\x%02X", c);
}

// ---------- Bit Writer ----------
typedef struct {
    FILE *f;
    unsigned char cur;
    int bits_filled;
} BitWriter;

static BitWriter *bitwriter_open(FILE *f) {
    BitWriter *bw = malloc(sizeof(BitWriter));
    bw->f = f; bw->cur = 0; bw->bits_filled = 0;
    return bw;
}

static void bitwriter_write_bits(BitWriter *bw, uint32_t bits, int nbits) {
    for (int i = nbits - 1; i >= 0; --i) {
        int bit = (bits >> i) & 1;
        bw->cur = (bw->cur << 1) | bit;
        bw->bits_filled++;
        if (bw->bits_filled == 8) {
            fwrite(&bw->cur, 1, 1, bw->f);
            bw->cur = 0; bw->bits_filled = 0;
        }
    }
}

static void bitwriter_close(BitWriter *bw) {
    if (bw->bits_filled > 0) {
        bw->cur <<= (8 - bw->bits_filled);
        fwrite(&bw->cur, 1, 1, bw->f);
    }
    free(bw);
}

// ---------- Symbol Counting ----------
typedef struct {
    unsigned char sym;
    uint64_t count;
} SymCount;

static int cmp_symcount(const void *a, const void *b) {
    const SymCount *A = a, *B = b;
    if (A->count < B->count) return -1;
    if (A->count > B->count) return 1;
    return (int)A->sym - (int)B->sym;
}

static int ceil_log2_u32(uint32_t x) {
    if (x <= 1) return 1;
    int l = 0; x -= 1;
    while (x > 0) { x >>= 1; l++; }
    return l;
}

// ---------- MAIN ----------
int main(int argc, char **argv) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s in_fn cb_fn enc_fn\n", argv[0]);
        return 1;
    }

    const char *in_fn = argv[1];
    const char *cb_fn = argv[2];
    const char *enc_fn = argv[3];

    FILE *fin = fopen(in_fn, "rb");
    if (!fin) { perror("fopen input"); return 1; }

    uint64_t counts[MAX_SYMBOLS] = {0}, total = 0;
    int c;
    while ((c = fgetc(fin)) != EOF) {
        counts[(unsigned char)c]++;
        total++;
    }
    fclose(fin);

    SymCount syms[MAX_SYMBOLS];
    int n = 0;
    for (int i = 0; i < MAX_SYMBOLS; ++i)
        if (counts[i] > 0) { syms[n].sym = i; syms[n].count = counts[i]; n++; }

    int L = ceil_log2_u32(n + 1); // +1 for EOF
    uint32_t eof_code = (1u << L) - 1;

    qsort(syms, n, sizeof(SymCount), cmp_symcount);

    FILE *fcb = fopen(cb_fn, "w");
    if (!fcb) { perror("fopen codebook"); return 1; }

    for (int i = 0; i < n; ++i) {
        char symrepr[16];
        symbol_to_csv_repr(syms[i].sym, symrepr, sizeof(symrepr));
        double prob = total ? (double)syms[i].count / total : 0.0;
        char codeword[64] = {0};
        for (int b = 0; b < L; ++b)
            codeword[b] = ((i >> (L - 1 - b)) & 1) ? '1' : '0';
        codeword[L] = '\0';
        fprintf(fcb, "\"%s\",%" PRIu64 ",%.7f,\"%s\"\n", symrepr, syms[i].count, prob, codeword);
    }
    fclose(fcb);

    fin = fopen(in_fn, "rb");
    if (!fin) { perror("fopen input again"); return 1; }
    FILE *fenc = fopen(enc_fn, "wb");
    if (!fenc) { perror("fopen encoded.bin"); fclose(fin); return 1; }
    BitWriter *bw = bitwriter_open(fenc);

    uint32_t map[256]; for (int i = 0; i < 256; ++i) map[i] = 0xFFFFFFFF;
    for (int i = 0; i < n; ++i) map[syms[i].sym] = i;

    while ((c = fgetc(fin)) != EOF) {
        unsigned char uc = c;
        uint32_t code = map[uc];
        bitwriter_write_bits(bw, code, L);
    }
    bitwriter_write_bits(bw, eof_code, L);

    bitwriter_close(bw);
    fclose(fin);
    fclose(fenc);
    return 0;
}
