/*
* Copyright 2021 European Union
*
* Licensed under the EUPL, Version 1.2 or – as soon they will be approved by 
* the European Commission - subsequent versions of the EUPL (the "Licence");
* You may not use this work except in compliance with the Licence.
* You may obtain a copy of the Licence at:
*
* https://joinup.ec.europa.eu/software/page/eupl
*
* Unless required by applicable law or agreed to in writing, software 
* distributed under the Licence is distributed on an "AS IS" basis,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the Licence for the specific language governing permissions and 
* limitations under the Licence.
*/ 

// This file is compiled just-in-time by the performance test and not integrated
// within the CMake infrastructure. Compiling it is trivial and would just
// add a lot of boilerplate.

#include <openssl/aes.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HASH_BYTES 12
#define HMAC_BYTES 4

// Implementation adopted from here:
// https://stackoverflow.com/a/37109258
static char * bin2hex(const unsigned char * bin, size_t len)
{
    char * out;
    size_t i;

    if (bin == NULL || len == 0) return NULL;

    out = malloc(len * 2 + 1);
    for (i = 0; i < len; i++) {
        out[i * 2] = "0123456789ABCDEF"[bin[i] >> 4];
        out[i * 2 + 1] = "0123456789ABCDEF"[bin[i] & 0x0F];
    }
    out[len * 2] = '\0';

    return out;
}

static char const B64chars[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static int const B64index[256] = {
        0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
        0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
        0,  0,  0,  0,  0,  0,  0,  62, 63, 62, 62, 63, 52, 53, 54, 55, 56, 57,
        58, 59, 60, 61, 0,  0,  0,  0,  0,  0,  0,  0,  1,  2,  3,  4,  5,  6,
        7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24,
        25, 0,  0,  0,  0,  63, 0,  26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36,
        37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51};

static void b64encode(unsigned char const data[16], char * const out)
{
    // These two statements could in theory be optimized somehow.
    int const len = 16;
    {
        int final_size = (len + 2) / 3 * 4;
        memset(out, '=', final_size);
        out[final_size] = '\n';
        out[final_size + 1] = '\0';
    }
    uint64_t j = 0u;
    uint64_t pad = len % 3;
    uint64_t const last = len - pad;

    for (uint64_t i = 0u; i < last; i += 3u) {
        int n = ((int)data[i]) << 16 | ((int)data[i + 1]) << 8 | data[i + 2];
        out[j++] = B64chars[n >> 18];
        out[j++] = B64chars[n >> 12 & 0x3F];
        out[j++] = B64chars[n >> 6 & 0x3F];
        out[j++] = B64chars[n & 0x3F];
    }
    if (pad) { /// set padding
        int n = --pad ? ((int)data[last]) << 8 | data[last + 1] : data[last];
        out[j++] = B64chars[pad ? n >> 10 & 0x3F : n >> 2];
        out[j++] = B64chars[pad ? n >> 4 & 0x03F : n << 4 & 0x3F];
        out[j++] = pad ? B64chars[n << 2 & 0x3F] : '=';
    }
}

static void hmac_sha256(const unsigned char key[16],
                        const unsigned char data[HASH_BYTES],
                        unsigned char result[HMAC_BYTES])
{
    unsigned char md[EVP_MAX_MD_SIZE];
    unsigned int resultlen = 0;
    unsigned char * mdp =
            HMAC(EVP_sha256(), key, 16, data, HASH_BYTES, md, &resultlen);
    if (!mdp) {
        fprintf(stderr, "HMAC failed");
        exit(1);
    }
    if (resultlen < HMAC_BYTES) {
        fprintf(stderr, "HMAC wrote too few bytes");
        exit(resultlen);
    }
    // printf("md: %s, mdp: %s\n", bin2hex(md, EVP_MAX_MD_SIZE), bin2hex(mdp, resultlen));
    memcpy(result, mdp, HMAC_BYTES);
}

static void encrypt(unsigned char plaintext[16],
                    unsigned char * key,
                    unsigned char ciphertext[16])
{
    EVP_CIPHER_CTX * ctx;

    int len = 17;

    int ciphertext_len;

    /* Create and initialise the context */
    if (!(ctx = EVP_CIPHER_CTX_new())) {
        fprintf(stderr, "EVP_CIPHER_CTX_new failed");
        exit(1);
    }

    unsigned char iv[16];
    memset(iv, 0, 16);
    if (1 != EVP_EncryptInit_ex(ctx, EVP_aes_128_ctr(), NULL, key, iv)) {
        fprintf(stderr, "EVP_EncryptInit_ex failed");
        exit(1);
    }

    if (1 != EVP_EncryptUpdate(ctx, ciphertext, &len, plaintext, 16)) {
        fprintf(stderr, "EVP_EncryptUpdate failed");
        exit(1);
    }
    if (len > 16) {
        fprintf(stderr, "EVP_EncryptUpdate wrote too much data");
        exit(1);
    }

    int len2 = 17;
    if (1 != EVP_EncryptFinal_ex(ctx, ciphertext + len, &len2)) {
        fprintf(stderr, "EVP_EncryptFinal_ex failed");
        exit(1);
    }
    if (len + len2 != 16) {
        fprintf(stderr, "EVP_EncryptFinal_ex wrote too much data");
        exit(1);
    }

    /* Clean up */
    EVP_CIPHER_CTX_free(ctx);
}

static int hexchr2bin(const char hex, char * out)
{
    if (out == NULL) return 0;

    if (hex >= '0' && hex <= '9') {
        *out = hex - '0';
    } else if (hex >= 'A' && hex <= 'F') {
        *out = hex - 'A' + 10;
    } else if (hex >= 'a' && hex <= 'f') {
        *out = hex - 'a' + 10;
    } else {
        return 0;
    }

    return 1;
}

static size_t hexs2bin(const char * hex, unsigned char * out)
{
    size_t len;
    char b1;
    char b2;
    size_t i;

    if (hex == NULL || *hex == '\0' || out == NULL) return 0;

    len = strlen(hex);
    if (len % 2 != 0) return 0;
    len /= 2;

    memset(out, 'A', len);
    for (i = 0; i < len; i++) {
        if (!hexchr2bin(hex[i * 2], &b1) || !hexchr2bin(hex[i * 2 + 1], &b2)) {
            return 0;
        }
        out[i] = (b1 << 4) | b2;
    }
    return len;
}

static int64_t S64(const char * s)
{
    int64_t i;
    char c;
    int scanned = sscanf(s, "%" SCNd64 "%c", &i, &c);
    if (scanned == 1) return i;
    if (scanned > 1) {
        // TBD about extra data found
        return i;
    }
    // TBD failed to scan;
    return 0;
}

static void sha256(const uint64_t in, unsigned char out[HASH_BYTES])
{
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256_CTX sha256;
    SHA256_Init(&sha256);
    SHA256_Update(&sha256, &in, sizeof(in));
    SHA256_Final(hash, &sha256);
    memcpy(out, hash, HASH_BYTES);
}

static void produce(unsigned char key[16], uint64_t id)
{
    struct pseudonym_and_mac_t {
        unsigned char pseudonym[HASH_BYTES];
        unsigned char mac[HMAC_BYTES];
    } plaintext;

    sha256(id, plaintext.pseudonym);
    hmac_sha256(key, plaintext.pseudonym, plaintext.mac);

    // printf("h: %s, mac: %s\n", bin2hex(plaintext.pseudonym, HASH_BYTES),
    // bin2hex(plaintext.mac, HMAC_BYTES));

    unsigned char plaintext_ar[16];
    unsigned char ciphertext[16];
    // static_assert(sizeof(plaintext) == 16);
    memcpy(plaintext_ar, &plaintext, 16);
    encrypt(plaintext_ar, key, ciphertext);
    char base64[30];
    b64encode(ciphertext, base64);
    // base64 already ends with \n\0, and base64 does not contain
    // printf-interpreted symbols, so just directly print it.
    printf(base64);
}

int main(int argc, char ** argv)
{
    if (strlen(argv[1]) != 32) {
        fprintf(stderr, "key hex wrong size");
        exit(strlen(argv[1]));
    }
    unsigned char key[16];
    hexs2bin(argv[1], key);
    uint64_t num_ids = S64(argv[2]);
    for (uint64_t id = 0; id < num_ids; ++id) { produce(key, id + 1); }
}
