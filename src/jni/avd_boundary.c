/*
 * avd_boundary.c - self-contained SHA-256 verification of the AVD-captured
 * nativeInitClientSettings arg0. No OpenSSL dependency, no guest access.
 */
#define _GNU_SOURCE
#include "avd_boundary.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* ---- compact SHA-256 (FIPS 180-4) --------------------------------------- */
static const uint32_t K[64] = {
    0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,
    0x923f82a4u,0xab1c5ed5u,0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,
    0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,0xe49b69c1u,0xefbe4786u,
    0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
    0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,
    0x06ca6351u,0x14292967u,0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,
    0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,0xa2bfe8a1u,0xa81a664bu,
    0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
    0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,
    0x5b9cca4fu,0x682e6ff3u,0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,
    0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u
};

#define ROR(x,n) (((x) >> (n)) | ((x) << (32 - (n))))
#define CH(x,y,z)  (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x,y,z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))

static void sha256_compress(uint32_t h[8], const unsigned char block[64])
{
    uint32_t w[64], a, b, c, d, e, f, g, hh, t1, t2;
    int i;
    for (i = 0; i < 16; ++i)
        w[i] = ((uint32_t)block[i*4] << 24) | ((uint32_t)block[i*4+1] << 16) |
               ((uint32_t)block[i*4+2] << 8) | (uint32_t)block[i*4+3];
    for (i = 16; i < 64; ++i) {
        uint32_t s0 = ROR(w[i-15],7) ^ ROR(w[i-15],18) ^ (w[i-15] >> 3);
        uint32_t s1 = ROR(w[i-2],17) ^ ROR(w[i-2],19) ^ (w[i-2] >> 10);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    a=h[0]; b=h[1]; c=h[2]; d=h[3]; e=h[4]; f=h[5]; g=h[6]; hh=h[7];
    for (i = 0; i < 64; ++i) {
        uint32_t S1 = ROR(e,6) ^ ROR(e,11) ^ ROR(e,25);
        uint32_t ch = CH(e,f,g);
        t1 = hh + S1 + ch + K[i] + w[i];
        uint32_t S0 = ROR(a,2) ^ ROR(a,13) ^ ROR(a,22);
        t2 = MAJ(a,b,c) + S0;
        hh=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
    }
    h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d; h[4]+=e; h[5]+=f; h[6]+=g; h[7]+=hh;
}

static void sha256(const unsigned char *data, size_t len, unsigned char out[32])
{
    uint32_t h[8] = {0x6a09e667u,0xbb67ae85u,0x3c6ef372u,0xa54ff53au,
                     0x510e527fu,0x9b05688cu,0x1f83d9abu,0x5be0cd19u};
    size_t i = 0;
    unsigned char block[64];
    while (len - i >= 64) { sha256_compress(h, data + i); i += 64; }
    size_t rem = len - i;
    memset(block, 0, sizeof(block));
    memcpy(block, data + i, rem);
    block[rem] = 0x80;
    if (rem >= 56) {
        sha256_compress(h, block);
        memset(block, 0, sizeof(block));
    }
    uint64_t bits = (uint64_t)len * 8u;
    for (i = 0; i < 8; ++i) block[56 + i] = (unsigned char)(bits >> (56 - 8*i));
    sha256_compress(h, block);
    for (i = 0; i < 8; ++i) {
        out[i*4]   = (unsigned char)(h[i] >> 24);
        out[i*4+1] = (unsigned char)(h[i] >> 16);
        out[i*4+2] = (unsigned char)(h[i] >> 8);
        out[i*4+3] = (unsigned char)h[i];
    }
}

/* ---- public API --------------------------------------------------------- */
int sober_avd_boundary_enabled(void)
{
    const char *v = getenv(SOBER_AVD_BOUNDARY_ENV);
    return v != NULL && strcmp(v, "1") == 0;
}

int sober_avd_boundary_should_withhold(void)
{
    return sober_avd_boundary_enabled();
}

int sober_avd_boundary_load(char **buf, size_t *len)
{
    if (!buf || !len) return -1;
    *buf = NULL; *len = 0;
    if (!sober_avd_boundary_enabled()) return -2;

    int modified = getenv(SOBER_AVD_BOUNDARY_UNVERIFIED) != NULL;
    const char *path = modified ? getenv(SOBER_AVD_BOUNDARY_PATH_ENV) : NULL;
    if (!path || !*path) path = SOBER_AVD_BOUNDARY_PATH;
    if (modified)
        fprintf(stderr, "[avd-boundary] MODIFIED INPUT, diagnostic only: %s "
                        "(never counts toward any checkpoint rung)\n", path);

    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "[avd-boundary] open failed: %s\n", path); return -3; }
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return -4; }
    long sz = ftell(f);
    if (sz <= 0 || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return -5; }
    char *data = (char *)malloc((size_t)sz);
    if (!data) { fclose(f); return -6; }
    if (fread(data, 1, (size_t)sz, f) != (size_t)sz) {
        free(data); fclose(f); return -7;
    }
    fclose(f);

    unsigned char digest[32];
    sha256((const unsigned char *)data, (size_t)sz, digest);
    char hex[65];
    for (int i = 0; i < 32; ++i) snprintf(hex + i*2, 3, "%02x", digest[i]);
    hex[64] = '\0';

    if (!modified && strcmp(hex, SOBER_AVD_BOUNDARY_SHA256) != 0) {
        fprintf(stderr, "[avd-boundary] SHA-256 MISMATCH got=%s want=%s\n",
                hex, SOBER_AVD_BOUNDARY_SHA256);
        free(data);
        return -8;
    }
    fprintf(stderr, "[avd-boundary] %s %s bytes sha256=%s%s\n",
            modified ? "MODIFIED INPUT, diagnostic only" : "verified",
            path, hex, modified ? " (UNVERIFIED, not a rung)" : "");
    *buf = data; *len = (size_t)sz;
    return 0;
}

void sober_avd_boundary_provenance(char *buf, size_t cap)
{
    if (!buf || !cap) return;
    snprintf(buf, cap,
             "class=\"%s\" session=\"%s\" file=%s sha256=%s argc=3 "
             "arg1=null arg2=\"%s\"",
             SOBER_AVD_BOUNDARY_CLASS, SOBER_AVD_BOUNDARY_SESSION,
             SOBER_AVD_BOUNDARY_PATH, SOBER_AVD_BOUNDARY_SHA256,
             SOBER_AVD_BOUNDARY_ARG2);
}
