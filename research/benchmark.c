#include <windows.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "api.h"
#include "crypto_aead.h"

#define MAX_MESSAGE 4096
#define MAX_AD 256
#define SAMPLES 30

static unsigned char key[CRYPTO_KEYBYTES];
static unsigned char nonce[CRYPTO_NPUBBYTES];
static unsigned char message[MAX_MESSAGE];
static unsigned char ad[MAX_AD];
static unsigned char ciphertext[MAX_MESSAGE + CRYPTO_ABYTES];
static volatile unsigned char output_sink;
static LARGE_INTEGER frequency;
static DWORD_PTR affinity;

static int pin_thread(void) {
  DWORD_PTR process_mask, system_mask;
  if (!GetProcessAffinityMask(GetCurrentProcess(), &process_mask, &system_mask) ||
      process_mask == 0) {
    fprintf(stderr, "GetProcessAffinityMask failed: %lu\n", GetLastError());
    return 0;
  }
  affinity = process_mask & (~process_mask + 1);
  if (!SetThreadAffinityMask(GetCurrentThread(), affinity)) {
    fprintf(stderr, "SetThreadAffinityMask failed: %lu\n", GetLastError());
    return 0;
  }
  return 1;
}

static int measure(unsigned int mlen, unsigned int adlen, unsigned int iterations,
                   long long* ticks) {
  LARGE_INTEGER start, end;
  unsigned long long clen = 0;
  unsigned int i;
  if (!QueryPerformanceCounter(&start)) return 0;
  for (i = 0; i < iterations; ++i) {
    if (crypto_aead_encrypt(ciphertext, &clen, message, mlen, ad, adlen, NULL,
                            nonce, key) != 0 ||
        clen != mlen + CRYPTO_ABYTES) {
      fprintf(stderr, "Encryption failed for plaintext=%u, AD=%u\n", mlen, adlen);
      return 0;
    }
  }
  if (!QueryPerformanceCounter(&end) || end.QuadPart < start.QuadPart) {
    fprintf(stderr, "QueryPerformanceCounter failed or went backwards.\n");
    return 0;
  }
  output_sink ^= ciphertext[0];
  *ticks = end.QuadPart - start.QuadPart;
  return 1;
}

static int run_case(unsigned int mlen, unsigned int adlen, int run, int smoke) {
  unsigned int iterations = 1, batch, trial;
  long long ticks, shortest;
  LARGE_INTEGER overhead_start, overhead_end;

  if (!measure(mlen, adlen, 32, &ticks)) return 0;
  if (!smoke) {
    do {
      shortest = frequency.QuadPart;
      for (trial = 0; trial < 3; ++trial) {
        if (!measure(mlen, adlen, iterations, &ticks)) return 0;
        if (ticks < shortest) shortest = ticks;
      }
      if (shortest >= frequency.QuadPart / 500 || iterations >= 1048576) break;
      iterations *= 2;
    } while (1);
    if (shortest < frequency.QuadPart / 500) {
      fprintf(stderr, "Unable to calibrate a 2 ms batch.\n");
      return 0;
    }
  } else {
    iterations = 32;
  }

  for (batch = 1; batch <= (smoke ? 1 : SAMPLES); ++batch) {
    if (!QueryPerformanceCounter(&overhead_start) ||
        !QueryPerformanceCounter(&overhead_end) ||
        overhead_end.QuadPart < overhead_start.QuadPart) {
      fprintf(stderr, "Timer overhead measurement failed.\n");
      return 0;
    }
    if (!measure(mlen, adlen, iterations, &ticks) || ticks == 0) {
      fprintf(stderr, "Timed batch did not advance the counter.\n");
      return 0;
    }
    printf("%s,%s,%s,%d,%u,%u,%u,%u,%lld,%lld,%lld,%llu\n",
           ASCON_ALGORITHM, ASCON_IMPLEMENTATION, ASCON_OPTIMIZATION, run, mlen,
           adlen, batch, iterations, ticks, frequency.QuadPart,
           overhead_end.QuadPart - overhead_start.QuadPart,
           (unsigned long long)affinity);
  }
  return 1;
}

int main(int argc, char** argv) {
  static const unsigned int lengths[] = {0, 1, 7, 8, 9, 15, 16, 17,
                                         31, 32, 33, 64, 1536, 4096};
  static const unsigned int ad_messages[] = {1, 16, 64, 1536};
  static const unsigned int ad_lengths[] = {1, 8, 16, 32, 256};
  unsigned int i, j;
  int run = 1, smoke = 0;
  char* end;
  long parsed;

  if (argc != 2) {
    fprintf(stderr, "Usage: benchmark RUN_NUMBER | --smoke\n");
    return 1;
  }
  if (strcmp(argv[1], "--smoke") == 0) {
    smoke = 1;
  } else {
    parsed = strtol(argv[1], &end, 10);
    if (*argv[1] == '\0' || *end != '\0' || parsed < 1 || parsed > 1000000) {
      fprintf(stderr, "RUN_NUMBER must be between 1 and 1000000.\n");
      return 1;
    }
    run = (int)parsed;
  }

#ifndef NDEBUG
  if (!smoke) {
    fprintf(stderr, "Performance measurements require a Release build.\n");
    return 1;
  }
#endif

  if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart < 500) {
    fprintf(stderr, "QueryPerformanceFrequency failed.\n");
    return 1;
  }
  if (!pin_thread()) return 1;
  for (i = 0; i < sizeof(key); ++i) key[i] = (unsigned char)i;
  for (i = 0; i < sizeof(nonce); ++i) nonce[i] = (unsigned char)i;
  for (i = 0; i < sizeof(message); ++i) message[i] = (unsigned char)i;
  for (i = 0; i < sizeof(ad); ++i) ad[i] = (unsigned char)i;

  if (smoke) return run_case(17, 9, run, 1) ? 0 : 1;
  for (i = 0; i < sizeof(lengths) / sizeof(lengths[0]); ++i)
    if (!run_case(lengths[i], 0, run, 0)) return 1;
  for (i = 0; i < sizeof(ad_messages) / sizeof(ad_messages[0]); ++i)
    for (j = 0; j < sizeof(ad_lengths) / sizeof(ad_lengths[0]); ++j)
      if (!run_case(ad_messages[i], ad_lengths[j], run, 0)) return 1;
  return 0;
}
