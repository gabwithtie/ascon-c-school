#include <stdio.h>
#include <string.h>

#include "api.h"
#include "crypto_aead.h"

#ifdef ASCON_INDEPENDENT
#include "ascon128.h"
#ifdef ASCON_INDEPENDENT_128A
#define independent_encrypt ascon128a_aead_encrypt
#define independent_decrypt ascon128a_aead_decrypt
#else
#define independent_encrypt ascon128_aead_encrypt
#define independent_decrypt ascon128_aead_decrypt
#endif
#endif

#define MAX_INPUT 256

static int matches_hex(const unsigned char* data, unsigned long long length,
                       const char* expected) {
  static const char digits[] = "0123456789ABCDEF";
  unsigned long long i;
  if (strlen(expected) != length * 2) return 0;
  for (i = 0; i < length; ++i) {
    if (expected[i * 2] != digits[data[i] >> 4] ||
        expected[i * 2 + 1] != digits[data[i] & 15])
      return 0;
  }
  return 1;
}

static int run_case(unsigned long long mlen, unsigned long long adlen) {
  unsigned char key[CRYPTO_KEYBYTES], nonce[CRYPTO_NPUBBYTES];
  unsigned char message[MAX_INPUT], ad[MAX_INPUT], plaintext[MAX_INPUT];
  unsigned char ciphertext[MAX_INPUT + CRYPTO_ABYTES];
  unsigned long long clen = 0, recovered = 0;
  unsigned int i;
#ifdef ASCON_INDEPENDENT
  unsigned char independent[MAX_INPUT + CRYPTO_ABYTES];
  unsigned long long independent_len = 0;
#endif

  for (i = 0; i < sizeof(key); ++i) key[i] = (unsigned char)i;
  for (i = 0; i < sizeof(nonce); ++i) nonce[i] = (unsigned char)i;
  for (i = 0; i < MAX_INPUT; ++i) message[i] = ad[i] = (unsigned char)i;

  if (crypto_aead_encrypt(ciphertext, &clen, message, mlen, ad, adlen, NULL,
                          nonce, key) != 0 ||
      clen != mlen + CRYPTO_ABYTES)
    return 0;

  /* v1.2.8 LWC AEAD KAT entries 1 and 571, also hosted by lightweight-crypto. */
#if ASCON_AEAD_RATE == 8
  if (mlen == 0 && adlen == 0 &&
      !matches_hex(ciphertext, clen, "E355159F292911F794CB1432A0103A8A"))
    return 0;
  if (mlen == 17 && adlen == 9 &&
      !matches_hex(ciphertext, clen,
                   "3225026599BCD4FCC460181575FA9D145BE9C8D3517D972AF3CEA382275CD02046"))
    return 0;
#elif ASCON_AEAD_RATE == 16
  if (mlen == 0 && adlen == 0 &&
      !matches_hex(ciphertext, clen, "7A834E6F09210957067B10FD831F0078"))
    return 0;
  if (mlen == 17 && adlen == 9 &&
      !matches_hex(ciphertext, clen,
                   "DF4B3A7282D646F3C994BF84C39D6C704C53A3814E93E6844A200FAD799D9C8FE6"))
    return 0;
#else
#error Unsupported Ascon AEAD rate
#endif

#ifdef ASCON_INDEPENDENT
  if (independent_encrypt(independent, &independent_len, message, mlen, ad,
                          adlen, NULL, nonce, key) != 0 ||
      independent_len != clen || memcmp(independent, ciphertext, clen) != 0)
    return 0;
  if (independent_decrypt(plaintext, &recovered, NULL, ciphertext, clen, ad,
                          adlen, nonce, key) != 0 ||
      recovered != mlen || memcmp(message, plaintext, mlen) != 0)
    return 0;
#endif

  if (crypto_aead_decrypt(plaintext, &recovered, NULL, ciphertext, clen, ad,
                          adlen, nonce, key) != 0 ||
      recovered != mlen || memcmp(message, plaintext, mlen) != 0)
    return 0;

  ciphertext[clen - 1] ^= 1;
  if (crypto_aead_decrypt(plaintext, &recovered, NULL, ciphertext, clen, ad,
                          adlen, nonce, key) == 0)
    return 0;
  ciphertext[clen - 1] ^= 1;

  if (mlen > 0) {
    ciphertext[0] ^= 1;
    if (crypto_aead_decrypt(plaintext, &recovered, NULL, ciphertext, clen, ad,
                            adlen, nonce, key) == 0)
      return 0;
    ciphertext[0] ^= 1;
  }
  if (adlen > 0) {
    ad[0] ^= 1;
    if (crypto_aead_decrypt(plaintext, &recovered, NULL, ciphertext, clen, ad,
                            adlen, nonce, key) == 0)
      return 0;
    ad[0] ^= 1;
  }
  nonce[0] ^= 1;
  if (crypto_aead_decrypt(plaintext, &recovered, NULL, ciphertext, clen, ad,
                          adlen, nonce, key) == 0)
    return 0;
  nonce[0] ^= 1;

  if (crypto_aead_decrypt(plaintext, &recovered, NULL, ciphertext,
                          CRYPTO_ABYTES - 1, ad, adlen, nonce, key) == 0)
    return 0;
  return 1;
}

int main(void) {
  static const unsigned int lengths[] = {0, 1, 7, 8, 9, 15, 16, 17,
                                         31, 32, 33, 64, 256};
  static const unsigned int ad_lengths[] = {0, 1, 7, 8, 9, 15, 16, 17, 32, 256};
  unsigned int i, j;
  for (i = 0; i < sizeof(lengths) / sizeof(lengths[0]); ++i) {
    for (j = 0; j < sizeof(ad_lengths) / sizeof(ad_lengths[0]); ++j) {
      if (!run_case(lengths[i], ad_lengths[j])) {
        fprintf(stderr, "Correctness failure: plaintext=%u, AD=%u\n",
                lengths[i], ad_lengths[j]);
        return 1;
      }
    }
  }
#ifdef ASCON_INDEPENDENT
  puts("130 cases passed, including independent implementation comparison.");
#else
  puts("130 cases passed; independent implementation was not configured.");
#endif
  return 0;
}
