/*
 * Copyright 2021 Bradley D. Nelson
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
*/

/*
 * ESP32forth v7.0.7.4
 * Revision: 595ed9e8296c64661139
 */

#include <Arduino.h>

#define STACK_CELLS 512
#define INTERRUPT_STACK_CELLS 64
#define MINIMUM_FREE_SYSTEM_HEAP (64 * 1024)

// Default on several options.
#define ENABLE_SPIFFS_SUPPORT
#define ENABLE_WIFI_SUPPORT
#define ENABLE_MDNS_SUPPORT
#define ENABLE_I2C_SUPPORT
#define ENABLE_SOCKETS_SUPPORT
#define ENABLE_FREERTOS_SUPPORT
#define ENABLE_INTERRUPTS_SUPPORT
#define ENABLE_LEDC_SUPPORT
#define ENABLE_SD_SUPPORT
#define ENABLE_SPI_FLASH_SUPPORT

// SD_MMC does not work on ESP32-S2 / ESP32-C3
#if !defined(CONFIG_IDF_TARGET_ESP32S2) && !defined(CONFIG_IDF_TARGET_ESP32C3)
# define ENABLE_SD_MMC_SUPPORT
#endif

// Serial2 does not work on ESP32-S2 / ESP32-C3
#if !defined(CONFIG_IDF_TARGET_ESP32S2) && !defined(CONFIG_IDF_TARGET_ESP32C3)
# define ENABLE_SERIAL2_SUPPORT
#endif

// No DACS on ESP32-S3 and ESP32-C3.
#if !defined(CONFIG_IDF_TARGET_ESP32S3) && !defined(CONFIG_IDF_TARGET_ESP32C3)
# define ENABLE_DAC_SUPPORT
#endif

// RMT support designed around v2.0.1 toolchain.
// While ESP32 also has RMT, for now only include for
// ESP32-S2 and ESP32-C3.
#if defined(CONFIG_IDF_TARGET_ESP32S2) || \
    defined(CONFIG_IDF_TARGET_ESP32C3) || \
    defined(SIM_PRINT_ONLY)
# define ENABLE_RMT_SUPPORT
#endif

// Uncomment this #define for OLED Support.
// You will need to install these libraries from the Library Manager:
//   Adafruit SSD1306
//   Adafruit GFX Library
//   Adafruit BusIO
//#define ENABLE_OLED_SUPPORT

// For now assume only boards with PSRAM should enable
// camera support and BluetoothSerial.
// ESP32-CAM always have PSRAM, but so do WROVER boards,
// so this isn't an ideal indicator.
// Some boards (e.g. ESP32-S2-WROVER) don't seem to have
// built the serial library, so check if its enabled as well.
#if defined(BOARD_HAS_PSRAM) || defined(SIM_PRINT_ONLY)
# define ENABLE_CAMERA_SUPPORT
# if (defined(CONFIG_BT_ENABLED) && \
      defined(CONFIG_BLUEDROID_ENABLED)) || \
     defined(SIM_PRINT_ONLY)
#  define ENABLE_SERIAL_BLUETOOTH_SUPPORT
#undef ENABLE_SERIAL_BLUETOOTH_SUPPORT // definitely helped - kludge tnr nov 20 2022
# endif
#endif

#if !defined(USER_VOCABULARIES)
# define USER_VOCABULARIES
#endif

#define VOCABULARY_LIST \
  V(forth) V(internals) \
  V(rtos) V(SPIFFS) V(serial) V(SD) V(SD_MMC) V(ESP) \
  V(ledc) V(Wire) V(WiFi) V(bluetooth) V(sockets) V(oled) \
  V(rmt) V(interrupts) V(spi_flash) V(camera) V(timers) \
  USER_VOCABULARIES
#include <inttypes.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef intptr_t cell_t;
typedef uintptr_t ucell_t;

#define XV(flags, name, op, code) Z(flags, name, op, code)
#define YV(flags, op, code) Z(flags, #op, op, code)
#define X(name, op, code) Z(forth, name, op, code)
#define Y(op, code) Z(forth, #op, op, code)

#define NIP (--sp)
#define NIPn(n) (sp -= (n))
#define DROP (tos = *sp--)
#define DROPn(n) (NIPn(n-1), DROP)
#define DUP (*++sp = tos)
#define PUSH DUP; tos = (cell_t)

#define PARK   DUP; *++rp = (cell_t) fp; *++rp = (cell_t) sp; *++rp = (cell_t) ip
#define UNPARK ip = (cell_t *) *rp--; sp = (cell_t *) *rp--; fp = (float *) *rp--; DROP

#define TOFLAGS(xt) ((uint8_t *) (((cell_t *) (xt)) - 1))
#define TONAMELEN(xt) (TOFLAGS(xt) + 1)
#define TOPARAMS(xt) ((uint16_t *) (TOFLAGS(xt) + 2))
#define TOSIZE(xt) (CELL_ALIGNED(*TONAMELEN(xt)) + sizeof(cell_t) * (3 + *TOPARAMS(xt)))
#define TOLINK(xt) (((cell_t *) (xt)) - 2)
#define TONAME(xt) ((*TOFLAGS(xt) & BUILTIN_MARK) ? (*(char **) TOLINK(xt)) \
    : (((char *) TOLINK(xt)) - CELL_ALIGNED(*TONAMELEN(xt))))
#define TOBODY(xt) (((cell_t *) xt) + ((void *) *((cell_t *) xt) == ADDROF(DOCREATE) || \
                                       (void *) *((cell_t *) xt) == ADDROF(DODOES) ? 2 : 1))

#ifndef COMMA
# define COMMA(n) *g_sys->heap++ = (cell_t) (n)
# define CCOMMA(n) *(uint8_t *) g_sys->heap = (n); \
                   g_sys->heap = (cell_t *) (1 + ((cell_t) g_sys->heap));
# define DOES(ip) **g_sys->current = (cell_t) ADDROF(DODOES); (*g_sys->current)[1] = (cell_t) ip
# define DOIMMEDIATE() *TOFLAGS(*g_sys->current) |= IMMEDIATE
# define UNSMUDGE() *TOFLAGS(*g_sys->current) &= ~SMUDGE; finish()
#endif

#ifndef SSMOD_FUNC
# if __SIZEOF_POINTER__ == 8
typedef __int128_t dcell_t;
# elif __SIZEOF_POINTER__ == 4 || defined(_M_IX86)
typedef int64_t dcell_t;
# else
#  error "unsupported cell size"
# endif
# define SSMOD_FUNC dcell_t d = (dcell_t) *sp * (dcell_t) sp[-1]; \
                    --sp; cell_t a = (cell_t) (d < 0 ? ~(~d / tos) : d / tos); \
                    *sp = (cell_t) (d - ((dcell_t) a) * tos); tos = a
#endif

typedef struct {
  const char *name;
  union {
    struct {
      uint8_t flags, name_length;
      uint16_t vocabulary;
    };
    cell_t multi;  // Forces cell alignment throughout.
  };
  const void *code;
} BUILTIN_WORD;

#define TIER0_OPCODE_LIST \
  YV(internals, NOP, ) \
  X("0=", ZEQUAL, tos = !tos ? -1 : 0) \
  X("0<", ZLESS, tos = (tos|0) < 0 ? -1 : 0) \
  X("+", PLUS, tos += *sp--) \
  X("U/MOD", USMOD, w = *sp; *sp = (ucell_t) w % (ucell_t) tos; \
                    tos = (ucell_t) w / (ucell_t) tos) \
  X("*/MOD", SSMOD, SSMOD_FUNC) \
  Y(LSHIFT, tos = (*sp << tos); --sp) \
  Y(RSHIFT, tos = (((ucell_t) *sp) >> tos); --sp) \
  Y(ARSHIFT, tos = (*sp >> tos); --sp) \
  Y(AND, tos &= *sp--) \
  Y(OR, tos |= *sp--) \
  Y(XOR, tos ^= *sp--) \
  X("DUP", ALTDUP, DUP) \
  Y(SWAP, w = tos; tos = *sp; *sp = w) \
  Y(OVER, DUP; tos = sp[-1]) \
  X("DROP", ALTDROP, DROP) \
  X("@", AT, tos = *(cell_t *) tos) \
  X("SL@", SLAT, tos = *(int32_t *) tos) \
  X("UL@", ULAT, tos = *(uint32_t *) tos) \
  X("SW@", SWAT, tos = *(int16_t *) tos) \
  X("UW@", UWAT, tos = *(uint16_t *) tos) \
  X("C@", CAT, tos = *(uint8_t *) tos) \
  X("!", STORE, *(cell_t *) tos = *sp--; DROP) \
  X("L!", LSTORE, *(int32_t *) tos = *sp--; DROP) \
  X("W!", WSTORE, *(int16_t *) tos = *sp--; DROP) \
  X("C!", CSTORE, *(uint8_t *) tos = *sp--; DROP) \
  X("SP@", SPAT, DUP; tos = (cell_t) sp) \
  X("SP!", SPSTORE, sp = (cell_t *) tos; DROP) \
  X("RP@", RPAT, DUP; tos = (cell_t) rp) \
  X("RP!", RPSTORE, rp = (cell_t *) tos; DROP) \
  X(">R", TOR, *++rp = tos; DROP) \
  X("R>", FROMR, DUP; tos = *rp; --rp) \
  X("R@", RAT, DUP; tos = *rp) \
  Y(EXECUTE, w = tos; DROP; JMPW) \
  YV(internals, BRANCH, ip = (cell_t *) *ip) \
  YV(internals, 0BRANCH, if (!tos) ip = (cell_t *) *ip; else ++ip; DROP) \
  YV(internals, DONEXT, *rp = *rp - 1; if (~*rp) ip = (cell_t *) *ip; else (--rp, ++ip)) \
  YV(internals, DOLIT, DUP; tos = *ip++) \
  YV(internals, DOSET, *((cell_t *) *ip) = tos; ++ip; DROP) \
  YV(internals, DOCOL, ++rp; *rp = (cell_t) ip; ip = (cell_t *) (w + sizeof(cell_t))) \
  YV(internals, DOCON, DUP; tos = *(cell_t *) (w + sizeof(cell_t))) \
  YV(internals, DOVAR, DUP; tos = w + sizeof(cell_t)) \
  YV(internals, DOCREATE, DUP; tos = w + sizeof(cell_t) * 2) \
  YV(internals, DODOES, DUP; tos = w + sizeof(cell_t) * 2; \
                        ++rp; *rp = (cell_t) ip; \
                        ip = (cell_t *) *(cell_t *) (w + sizeof(cell_t))) \
  YV(internals, ALITERAL, COMMA(g_sys->DOLIT_XT); COMMA(tos); DROP) \
  Y(CELL, DUP; tos = sizeof(cell_t)) \
  XV(internals, "LONG-SIZE", LONG_SIZE, DUP; tos = sizeof(long)) \
  Y(FIND, tos = find((const char *) *sp, tos); --sp) \
  Y(PARSE, DUP; tos = parse(tos, sp)) \
  XV(internals, "S>NUMBER?", \
      CONVERT, tos = convert((const char *) *sp, tos, g_sys->base, sp); \
      if (!tos) --sp) \
  Y(CREATE, DUP; DUP; tos = parse(32, sp); \
            create((const char *) *sp, tos, 0, ADDROF(DOCREATE)); \
            COMMA(0); DROPn(2)) \
  Y(VARIABLE, DUP; DUP; tos = parse(32, sp); \
              create((const char *) *sp, tos, 0, ADDROF(DOVAR)); \
              COMMA(0); DROPn(2)) \
  Y(CONSTANT, DUP; DUP; tos = parse(32, sp); \
              create((const char *) *sp, tos, 0, ADDROF(DOCON)); \
              DROPn(2); COMMA(tos); DROP) \
  X("DOES>", DOES, DOES(ip); ip = (cell_t *) *rp; --rp) \
  Y(IMMEDIATE, DOIMMEDIATE()) \
  X(">BODY", TOBODY, tos = (cell_t) TOBODY(tos)) \
  XV(internals, "'SYS", SYS, DUP; tos = (cell_t) g_sys) \
  YV(internals, YIELD, PARK; return rp) \
  X(":", COLON, DUP; DUP; tos = parse(32, sp); \
                create((const char *) *sp, tos, SMUDGE, ADDROF(DOCOL)); \
                g_sys->state = -1; --sp; DROP) \
  YV(internals, EVALUATE1, PARK; rp = evaluate1(rp); UNPARK; w = tos; DROP; if (w) JMPW) \
  Y(EXIT, ip = (cell_t *) *rp--) \
  XV(internals, "'builtins", TBUILTINS, DUP; tos = (cell_t) &g_sys->builtins->code) \
  XV(forth_immediate, ";", SEMICOLON, COMMA(g_sys->DOEXIT_XT); UNSMUDGE(); g_sys->state = 0)
#define TIER1_OPCODE_LIST \
  Y(nip, NIP) \
  Y(rdrop, --rp) \
  XV(forth, "*/", STARSLASH, SSMOD_FUNC; NIP) \
  X("*", STAR, tos *= *sp--) \
  X("/mod", SLASHMOD, DUP; *sp = 1; SSMOD_FUNC) \
  X("/", SLASH, DUP; *sp = 1; SSMOD_FUNC; NIP) \
  Y(mod, DUP; *sp = 1; SSMOD_FUNC; DROP) \
  Y(invert, tos = ~tos) \
  Y(negate, tos = -tos) \
  X("-", MINUS, tos = (*sp--) - tos) \
  Y(rot, w = sp[-1]; sp[-1] = *sp; *sp = tos; tos = w) \
  X("-rot", MROT, w = tos; tos = *sp; *sp = sp[-1]; sp[-1] = w) \
  X("?dup", QDUP, if (tos) DUP) \
  X("<", LESS, tos = *sp < tos ? -1 : 0; --sp) \
  X(">", GREATER, tos = *sp > tos ? -1 : 0; --sp) \
  X("<=", LESSEQ, tos = *sp <= tos ? -1 : 0; --sp) \
  X(">=", GREATEREQ, tos = *sp >= tos ? -1 : 0; --sp) \
  X("=", EQUAL, tos = *sp == tos ? -1 : 0; --sp) \
  X("<>", NOTEQUAL, tos = *sp != tos ? -1 : 0; --sp) \
  X("0<>", ZNOTEQUAL, tos = tos ? -1 : 0) \
  Y(bl, DUP; tos = ' ') \
  Y(nl, DUP; tos = '\n') \
  X("1+", ONEPLUS, ++tos) \
  X("1-", ONEMINUS, --tos) \
  X("2*", TWOSTAR, tos = tos << 1) \
  X("2/", TWOSLASH, tos = tos >> 1) \
  X("4*", FOURSTAR, tos = tos << 2) \
  X("4/", FOURSLASH, tos = tos >> 2) \
  X("+!", PLUSSTORE, *((cell_t *) tos) += *sp--; DROP) \
  X("cell+", CELLPLUS, tos += sizeof(cell_t)) \
  Y(cells, tos *= sizeof(cell_t)) \
  X("cell/", CELLSLASH, DUP; tos = sizeof(cell_t); DUP; *sp = 1; SSMOD_FUNC; NIP) \
  X("2drop", TWODROP, NIP; DROP) \
  X("2dup", TWODUP, DUP; tos = sp[-1]; DUP; tos = sp[-1]) \
  X("2@", TWOAT, DUP; *sp = *(cell_t *) tos; tos = ((cell_t *) tos)[1]) \
  X("2!", TWOSTORE, *(cell_t *) tos = sp[-1]; \
      ((cell_t *) tos)[1] = *sp; sp -= 2; DROP) \
  Y(cmove, memmove((void *) *sp, (void *) sp[-1], tos); sp -= 2; DROP) \
  X("cmove>", cmove2, memmove((void *) *sp, (void *) sp[-1], tos); sp -= 2; DROP) \
  Y(fill, memset((void *) sp[-1], tos, *sp); sp -= 2; DROP) \
  Y(erase, memset((void *) *sp, 0, tos); NIP; DROP) \
  Y(blank, memset((void *) *sp, ' ', tos); NIP; DROP) \
  Y(min, tos = tos < *sp ? tos : *sp; NIP) \
  Y(max, tos = tos > *sp ? tos : *sp; NIP) \
  Y(abs, tos = tos < 0 ? -tos : tos) \
  Y(here, DUP; tos = (cell_t) g_sys->heap) \
  Y(allot, g_sys->heap = (cell_t *) (tos + (cell_t) g_sys->heap); DROP) \
  X(",", COMMA, COMMA(tos); DROP) \
  X("c,", CCOMMA, CCOMMA(tos); DROP) \
  XV(internals, "'heap", THEAP, DUP; tos = (cell_t) &g_sys->heap) \
  Y(current, DUP; tos = (cell_t) &g_sys->current) \
  XV(internals, "'context", TCONTEXT, DUP; tos = (cell_t) &g_sys->context) \
  XV(internals, "'latestxt", TLATESTXT, DUP; tos = (cell_t) &g_sys->latestxt) \
  XV(internals, "'notfound", TNOTFOUND, DUP; tos = (cell_t) &g_sys->notfound) \
  XV(internals, "'heap-start", THEAP_START, DUP; tos = (cell_t) &g_sys->heap_start) \
  XV(internals, "'heap-size", THEAP_SIZE, DUP; tos = (cell_t) &g_sys->heap_size) \
  XV(internals, "'stack-cells", TSTACK_CELLS, DUP; tos = (cell_t) &g_sys->stack_cells) \
  XV(internals, "'boot", TBOOT, DUP; tos = (cell_t) &g_sys->boot) \
  XV(internals, "'boot-size", TBOOT_SIZE, DUP; tos = (cell_t) &g_sys->boot_size) \
  XV(internals, "'tib", TTIB, DUP; tos = (cell_t) &g_sys->tib) \
  X("#tib", NTIB, DUP; tos = (cell_t) &g_sys->ntib) \
  X(">in", TIN, DUP; tos = (cell_t) &g_sys->tin) \
  Y(state, DUP; tos = (cell_t) &g_sys->state) \
  Y(base, DUP; tos = (cell_t) &g_sys->base) \
  XV(internals, "'argc", ARGC, DUP; tos = (cell_t) &g_sys->argc) \
  XV(internals, "'argv", ARGV, DUP; tos = (cell_t) &g_sys->argv) \
  XV(internals, "'runner", RUNNER, DUP; tos = (cell_t) &g_sys->runner) \
  Y(context, DUP; tos = (cell_t) (g_sys->context + 1)) \
  Y(latestxt, DUP; tos = (cell_t) g_sys->latestxt) \
  XV(forth_immediate, "[", LBRACKET, g_sys->state = 0) \
  XV(forth_immediate, "]", RBRACKET, g_sys->state = -1) \
  YV(forth_immediate, literal, COMMA(g_sys->DOLIT_XT); COMMA(tos); DROP)
#define TIER2_OPCODE_LIST \
  X(">flags", TOFLAGS, tos = *TOFLAGS(tos)) \
  X(">flags&", TOFLAGSAT, tos = (cell_t) TOFLAGS(tos)) \
  X(">params", TOPARAMS, tos = *TOPARAMS(tos)) \
  X(">size", TOSIZE, tos = TOSIZE(tos)) \
  X(">link&", TOLINKAT, tos = (cell_t) TOLINK(tos)) \
  X(">link", TOLINK, tos = *TOLINK(tos)) \
  X(">name", TONAME, DUP; *sp = (cell_t) TONAME(tos); tos = *TONAMELEN(tos)) \
  Y(aligned, tos = CELL_ALIGNED(tos)) \
  Y(align, g_sys->heap = (cell_t *) CELL_ALIGNED(g_sys->heap)) \
  YV(internals, fill32, cell_t c = tos; DROP; cell_t n = tos; DROP; \
                        uint32_t *a = (uint32_t *) tos; DROP; \
                        for (;n;--n) *a++ = c)
#include <math.h>

#define FLOATING_POINT_LIST \
  YV(internals, DOFLIT, *++fp = *(float *) ip; ++ip) \
  X("FP@", FPAT, DUP; tos = (cell_t) fp) \
  X("FP!", FPSTORE, fp = (float *) tos; DROP) \
  X("SF@", FAT, *++fp = *(float *) tos; DROP) \
  X("SF!", FSTORE, *(float *) tos = *fp--; DROP) \
  Y(FDUP, fp[1] = *fp; ++fp) \
  Y(FNIP, fp[-1] = *fp; --fp) \
  Y(FDROP, --fp) \
  Y(FOVER, fp[1] = fp[-1]; ++fp) \
  Y(FSWAP, ft = fp[-1]; fp[-1] = *fp; *fp = ft) \
  Y(FROT, ft = fp[-2]; fp[-2] = fp[-1]; fp[-1] = *fp; *fp = ft) \
  Y(FNEGATE, *fp = -*fp) \
  X("F0<", FZLESS, DUP; tos = *fp < 0.0f ? -1 : 0; --fp) \
  X("F0=", FZEQUAL, DUP; tos = *fp == 0.0f ? -1 : 0; --fp) \
  X("F=", FEQUAL, DUP; tos = fp[-1] == fp[0] ? -1 : 0; fp -= 2) \
  X("F<", FLESS, DUP; tos = fp[-1] < fp[0] ? -1 : 0; fp -= 2) \
  X("F>", FGREATER, DUP; tos = fp[-1] > fp[0] ? -1 : 0; fp -= 2) \
  X("F<>", FNEQUAL, DUP; tos = fp[-1] != fp[0] ? -1 : 0; fp -= 2) \
  X("F<=", FLESSEQ, DUP; tos = fp[-1] <= fp[0] ? -1 : 0; fp -= 2) \
  X("F>=", FGREATEREQ, DUP; tos = fp[-1] >= fp[0] ? -1 : 0; fp -= 2) \
  X("F+", FPLUS, fp[-1] = fp[-1] + *fp; --fp) \
  X("F-", FMINUS, fp[-1] = fp[-1] - *fp; --fp) \
  X("F*", FSTAR, fp[-1] = fp[-1] * *fp; --fp) \
  X("F/", FSLASH, fp[-1] = fp[-1] / *fp; --fp) \
  X("1/F", FINVERSE, *fp = 1.0f / *fp) \
  X("S>F", STOF, *++fp = (float) tos; DROP) \
  X("F>S", FTOS, DUP; tos = (cell_t) *fp--) \
  XV(internals, "S>FLOAT?", FCONVERT, tos = fconvert((const char *) *sp, tos, fp)|0; --sp) \
  Y(SFLOAT, DUP; tos = sizeof(float)) \
  Y(SFLOATS, tos *= sizeof(float)) \
  X("SFLOAT+", SFLOATPLUS, tos += sizeof(float)) \
  X("PI", PI_CONST, *++fp = (float) 3.14159265359) \
  Y(FSIN, *fp = sin(+*fp)) \
  Y(FCOS, *fp = cos(+*fp)) \
  Y(FSINCOS, fp[1] = cos(+*fp); *fp = sin(+*fp); ++fp) \
  Y(FATAN2, fp[-1] = atan2(+fp[-1], +*fp); --fp) \
  X("F**", FSTARSTAR, fp[-1] = pow(+fp[-1], +*fp); --fp) \
  Y(FLOOR, *fp = floor(+*fp)) \
  Y(FEXP, *fp = exp(+*fp)) \
  Y(FLN, *fp = log(+*fp)) \
  Y(FABS, *fp = fabs(+*fp)) \
  Y(FMIN, fp[-1] = fmin(+fp[-1], +*fp); --fp) \
  Y(FMAX, fp[-1] = fmax(+fp[-1], +*fp); --fp) \
  Y(FSQRT, *fp = sqrt(+*fp))
#ifndef CALLTYPE
# define CALLTYPE
#endif

#ifdef __cplusplus
typedef cell_t (CALLTYPE *call_t)(...);
#else
typedef cell_t (CALLTYPE *call_t)();
#endif

#define ct0 ((call_t) n0)

#define CALLING_OPCODE_LIST \
  YV(internals, CALLCODE, float *t_fp = fp; DUP; \
      sp = (cell_t *) (*(call_t*) (w + sizeof(cell_t)))(sp, &t_fp); \
      fp = t_fp; DROP) \
  YV(internals, CALL0, n0 = ct0()) \
  YV(internals, CALL1, n0 = ct0(n1); --sp) \
  YV(internals, CALL2, n0 = ct0(n2, n1); sp -= 2) \
  YV(internals, CALL3, n0 = ct0(n3, n2, n1); sp -= 3) \
  YV(internals, CALL4, n0 = ct0(n4, n3, n2, n1); sp -= 4) \
  YV(internals, CALL5, n0 = ct0(n5, n4, n3, n2, n1); sp -= 5) \
  YV(internals, CALL6, n0 = ct0(n6, n5, n4, n3, n2, n1); sp -= 6) \
  YV(internals, CALL7, n0 = ct0(n7, n6, n5, n4, n3, n2, n1); sp -= 7) \
  YV(internals, CALL8, n0 = ct0(n8, n7, n6, n5, n4, n3, n2, n1); sp -= 8) \
  YV(internals, CALL9, n0 = ct0(n9, n8, n7, n6, n5, n4, n3, n2, n1); sp -= 9) \
  YV(internals, CALL10, n0 = ct0(n10, n9, n8, n7, n6, n5, n4, n3, n2, n1); sp -= 10) \
  YV(internals, CALL11, n0 = ct0(n11, n10, n9, n8, n7, n6, n5, n4, n3, n2, n1); sp -= 11) \
  YV(internals, CALL12, n0 = ct0(n12, n11, n10, n9, n8, n7, n6, n5, n4, n3, n2, n1); sp -= 12) \
  YV(internals, CALL13, n0 = ct0(n13, n12, n11, n10, n9, n8, n7, n6, n5, n4, n3, n2, n1); sp -= 13) \
  YV(internals, CALL14, n0 = ct0(n14, n13, n12, n11, n10, n9, n8, n7, n6, n5, n4, n3, n2, n1); sp -= 14) \
  YV(internals, CALL15, n0 = ct0(n15, n14, n13, n12, n11, n10, n9, n8, n7, n6, n5, n4, n3, n2, n1); sp -= 15)
#define SET tos = (cell_t)

#define n0 tos
#define n1 (*sp)
#define n2 sp[-1]
#define n3 sp[-2]
#define n4 sp[-3]
#define n5 sp[-4]
#define n6 sp[-5]
#define n7 sp[-6]
#define n8 sp[-7]
#define n9 sp[-8]
#define n10 sp[-9]
#define n11 sp[-10]
#define n12 sp[-11]
#define n13 sp[-12]
#define n14 sp[-13]
#define n15 sp[-14]

#define a0 ((void *) tos)
#define a1 (*(void **) &n1)
#define a2 (*(void **) &n2)
#define a3 (*(void **) &n3)
#define a4 (*(void **) &n4)
#define a5 (*(void **) &n5)
#define a6 (*(void **) &n6)

#define b0 ((uint8_t *) tos)
#define b1 (*(uint8_t **) &n1)
#define b2 (*(uint8_t **) &n2)
#define b3 (*(uint8_t **) &n3)
#define b4 (*(uint8_t **) &n4)
#define b5 (*(uint8_t **) &n5)
#define b6 (*(uint8_t **) &n6)

#define c0 ((char *) tos)
#define c1 (*(char **) &n1)
#define c2 (*(char **) &n2)
#define c3 (*(char **) &n3)
#define c4 (*(char **) &n4)
#define c5 (*(char **) &n5)
#define c6 (*(char **) &n6)
#ifndef SIM_PRINT_ONLY

# include <dirent.h>
# include <errno.h>
# include <unistd.h>
# include <fcntl.h>
# include <sys/types.h>
# include <sys/stat.h>
# include <sys/select.h>

// Optional hook to pull in words for userwords.h
# if __has_include("userwords.h")
#  include "userwords.h"
# else
#  define USER_WORDS
# endif

static cell_t ResizeFile(cell_t fd, cell_t size);

#endif

#define PLATFORM_OPCODE_LIST \
  USER_WORDS \
  REQUIRED_ESP_SUPPORT \
  REQUIRED_MEMORY_SUPPORT \
  REQUIRED_SERIAL_SUPPORT \
  OPTIONAL_SERIAL2_SUPPORT \
  REQUIRED_ARDUINO_GPIO_SUPPORT \
  REQUIRED_SYSTEM_SUPPORT \
  REQUIRED_FILES_SUPPORT \
  OPTIONAL_LEDC_SUPPORT \
  OPTIONAL_DAC_SUPPORT \
  OPTIONAL_SPIFFS_SUPPORT \
  OPTIONAL_WIFI_SUPPORT \
  OPTIONAL_MDNS_SUPPORT \
  OPTIONAL_SD_SUPPORT \
  OPTIONAL_SD_MMC_SUPPORT \
  OPTIONAL_I2C_SUPPORT \
  OPTIONAL_SERIAL_BLUETOOTH_SUPPORT \
  OPTIONAL_CAMERA_SUPPORT \
  OPTIONAL_SOCKETS_SUPPORT \
  OPTIONAL_FREERTOS_SUPPORT \
  OPTIONAL_INTERRUPTS_SUPPORT \
  OPTIONAL_RMT_SUPPORT \
  OPTIONAL_OLED_SUPPORT \
  OPTIONAL_SPI_FLASH_SUPPORT \
  CALLING_OPCODE_LIST \
  FLOATING_POINT_LIST

#define REQUIRED_MEMORY_SUPPORT \
  YV(internals, MALLOC, SET malloc(n0)) \
  YV(internals, SYSFREE, free(a0); DROP) \
  YV(internals, REALLOC, SET realloc(a1, n0); NIP) \
  YV(internals, heap_caps_malloc, SET heap_caps_malloc(n1, n0); NIP) \
  YV(internals, heap_caps_free, heap_caps_free(a0); DROP) \
  YV(internals, heap_caps_realloc, \
      tos = (cell_t) heap_caps_realloc(a2, n1, n0); NIPn(2))

#define REQUIRED_ESP_SUPPORT \
  YV(ESP, getHeapSize, PUSH ESP.getHeapSize()) \
  YV(ESP, getFreeHeap, PUSH ESP.getFreeHeap()) \
  YV(ESP, getMaxAllocHeap, PUSH ESP.getMaxAllocHeap()) \
  YV(ESP, getChipModel, PUSH ESP.getChipModel()) \
  YV(ESP, getChipCores, PUSH ESP.getChipCores()) \
  YV(ESP, getFlashChipSize, PUSH ESP.getFlashChipSize()) \
  YV(ESP, getCpuFreqMHz, PUSH ESP.getCpuFreqMHz()) \
  YV(ESP, getSketchSize, PUSH ESP.getSketchSize()) \
  YV(ESP, deepSleep, ESP.deepSleep(tos); DROP) \
  YV(ESP, getEfuseMac, PUSH (cell_t) ESP.getEfuseMac(); PUSH (cell_t) (ESP.getEfuseMac() >> 32)) \
  YV(ESP, esp_log_level_set, esp_log_level_set(c1, (esp_log_level_t) n0); DROPn(2))

#define REQUIRED_SYSTEM_SUPPORT \
  X("MS-TICKS", MS_TICKS, PUSH millis()) \
  XV(internals, "RAW-YIELD", RAW_YIELD, yield()) \
  Y(TERMINATE, exit(n0))

#define REQUIRED_SERIAL_SUPPORT \
  XV(serial, "Serial.begin", SERIAL_BEGIN, Serial.begin(tos); DROP) \
  XV(serial, "Serial.end", SERIAL_END, Serial.end()) \
  XV(serial, "Serial.available", SERIAL_AVAILABLE, PUSH Serial.available()) \
  XV(serial, "Serial.readBytes", SERIAL_READ_BYTES, n0 = Serial.readBytes(b1, n0); NIP) \
  XV(serial, "Serial.write", SERIAL_WRITE, n0 = Serial.write(b1, n0); NIP) \
  XV(serial, "Serial.flush", SERIAL_FLUSH, Serial.flush()) \
  XV(serial, "Serial.setDebugOutput", SERIAL_DEBUG_OUTPUT, Serial.setDebugOutput(n0); DROP)

#ifndef ENABLE_SERIAL2_SUPPORT
# define OPTIONAL_SERIAL2_SUPPORT
#else
# define OPTIONAL_SERIAL2_SUPPORT \
  XV(serial, "Serial2.begin", SERIAL2_BEGIN, Serial2.begin(tos); DROP) \
  XV(serial, "Serial2.end", SERIAL2_END, Serial2.end()) \
  XV(serial, "Serial2.available", SERIAL2_AVAILABLE, PUSH Serial2.available()) \
  XV(serial, "Serial2.readBytes", SERIAL2_READ_BYTES, n0 = Serial2.readBytes(b1, n0); NIP) \
  XV(serial, "Serial2.write", SERIAL2_WRITE, n0 = Serial2.write(b1, n0); NIP) \
  XV(serial, "Serial2.flush", SERIAL2_FLUSH, Serial2.flush()) \
  XV(serial, "Serial2.setDebugOutput", SERIAL2_DEBUG_OUTPUT, Serial2.setDebugOutput(n0); DROP)
#endif

#define REQUIRED_ARDUINO_GPIO_SUPPORT \
  Y(pinMode, pinMode(n1, n0); DROPn(2)) \
  Y(digitalWrite, digitalWrite(n1, n0); DROPn(2)) \
  Y(digitalRead, n0 = digitalRead(n0)) \
  Y(analogRead, n0 = analogRead(n0)) \
  Y(pulseIn, n0 = pulseIn(n2, n1, n0); NIPn(2))

#define REQUIRED_FILES_SUPPORT \
  X("R/O", R_O, PUSH O_RDONLY) \
  X("W/O", W_O, PUSH O_WRONLY) \
  X("R/W", R_W, PUSH O_RDWR) \
  Y(BIN, ) \
  X("CLOSE-FILE", CLOSE_FILE, tos = close(tos); tos = tos ? errno : 0) \
  X("FLUSH-FILE", FLUSH_FILE, fsync(tos); /* fsync has no impl and returns ENOSYS :-( */ tos = 0) \
  X("OPEN-FILE", OPEN_FILE, cell_t mode = n0; DROP; cell_t len = n0; DROP; \
    memcpy(filename, a0, len); filename[len] = 0; \
    n0 = open(filename, mode, 0777); PUSH n0 < 0 ? errno : 0) \
  X("CREATE-FILE", CREATE_FILE, cell_t mode = n0; DROP; cell_t len = n0; DROP; \
    memcpy(filename, a0, len); filename[len] = 0; \
    n0 = open(filename, mode | O_CREAT | O_TRUNC); PUSH n0 < 0 ? errno : 0) \
  X("DELETE-FILE", DELETE_FILE, cell_t len = n0; DROP; \
    memcpy(filename, a0, len); filename[len] = 0; \
    n0 = unlink(filename); n0 = n0 ? errno : 0) \
  X("RENAME-FILE", RENAME_FILE, \
    cell_t len = n0; DROP; memcpy(filename, a0, len); filename[len] = 0; DROP; \
    cell_t len2 = n0; DROP; memcpy(filename2, a0, len2); filename2[len2] = 0; \
    n0 = rename(filename2, filename); n0 = n0 ? errno : 0) \
  X("WRITE-FILE", WRITE_FILE, cell_t fd = n0; DROP; cell_t len = n0; DROP; \
    n0 = write(fd, a0, len); n0 = n0 != len ? errno : 0) \
  X("READ-FILE", READ_FILE, cell_t fd = n0; DROP; cell_t len = n0; DROP; \
    n0 = read(fd, a0, len); PUSH n0 < 0 ? errno : 0) \
  X("FILE-POSITION", FILE_POSITION, \
    n0 = (cell_t) lseek(n0, 0, SEEK_CUR); PUSH n0 < 0 ? errno : 0) \
  X("REPOSITION-FILE", REPOSITION_FILE, cell_t fd = n0; DROP; \
    n0 = (cell_t) lseek(fd, tos, SEEK_SET); n0 = n0 < 0 ? errno : 0) \
  X("RESIZE-FILE", RESIZE_FILE, cell_t fd = n0; DROP; n0 = ResizeFile(fd, tos)) \
  X("FILE-SIZE", FILE_SIZE, struct stat st; w = fstat(n0, &st); \
    n0 = (cell_t) st.st_size; PUSH w < 0 ? errno : 0) \
  X("NON-BLOCK", NON_BLOCK, n0 = fcntl(n0, F_SETFL, O_NONBLOCK); \
    n0 = n0 < 0 ? errno : 0) \
  X("OPEN-DIR", OPEN_DIR, memcpy(filename, a1, n0); filename[n0] = 0; \
    n1 = (cell_t) opendir(filename); n0 = n1 ? 0 : errno) \
  X("CLOSE-DIR", CLOSE_DIR, n0 = closedir((DIR *) n0); n0 = n0 ? errno : 0) \
  YV(internals, READDIR, \
    struct dirent *ent = readdir((DIR *) n0); SET (ent ? ent->d_name: 0))

#ifndef ENABLE_LEDC_SUPPORT
# define OPTIONAL_LEDC_SUPPORT
#else
# define OPTIONAL_LEDC_SUPPORT \
  YV(ledc, ledcSetup, \
      n0 = (cell_t) (1000000 * ledcSetup(n2, n1 / 1000.0, n0)); NIPn(2)) \
  YV(ledc, ledcAttachPin, ledcAttachPin(n1, n0); DROPn(2)) \
  YV(ledc, ledcDetachPin, ledcDetachPin(n0); DROP) \
  YV(ledc, ledcRead, n0 = ledcRead(n0)) \
  YV(ledc, ledcReadFreq, n0 = (cell_t) (1000000 * ledcReadFreq(n0))) \
  YV(ledc, ledcWrite, ledcWrite(n1, n0); DROPn(2)) \
  YV(ledc, ledcWriteTone, \
      n0 = (cell_t) (1000000 * ledcWriteTone(n1, n0 / 1000.0)); NIP) \
  YV(ledc, ledcWriteNote, \
      tos = (cell_t) (1000000 * ledcWriteNote(n2, (note_t) n1, n0)); NIPn(2))
#endif

#ifndef ENABLE_DAC_SUPPORT
# define OPTIONAL_DAC_SUPPORT
# else
# define OPTIONAL_DAC_SUPPORT \
  Y(dacWrite, dacWrite(n1, n0); DROPn(2))
#endif

#ifndef ENABLE_SPI_FLASH_SUPPORT
# define OPTIONAL_SPI_FLASH_SUPPORT
#else
# ifndef SIM_PRINT_ONLY
#  include "esp_spi_flash.h"
#  include "esp_partition.h"
# endif
# define OPTIONAL_SPI_FLASH_SUPPORT \
  YV(spi_flash, spi_flash_init, spi_flash_init()) \
  YV(spi_flash, spi_flash_get_chip_size, PUSH spi_flash_get_chip_size()) \
  YV(spi_flash, spi_flash_erase_sector, n0 = spi_flash_erase_sector(n0)) \
  YV(spi_flash, spi_flash_erase_range, n0 = spi_flash_erase_range(n1, n0); DROP) \
  YV(spi_flash, spi_flash_write, n0 = spi_flash_write(n2, a1, n0); NIPn(2)) \
  YV(spi_flash, spi_flash_write_encrypted, n0 = spi_flash_write_encrypted(n2, a1, n0); NIPn(2)) \
  YV(spi_flash, spi_flash_read, n0 = spi_flash_read(n2, a1, n0); NIPn(2)) \
  YV(spi_flash, spi_flash_read_encrypted, n0 = spi_flash_read_encrypted(n2, a1, n0); NIPn(2)) \
  YV(spi_flash, spi_flash_mmap, \
      n0 = spi_flash_mmap(n4, n3, (spi_flash_mmap_memory_t) n2, \
                          (const void **) a1, (spi_flash_mmap_handle_t *) a0); NIPn(4)) \
  YV(spi_flash, spi_flash_mmap_pages, \
      n0 = spi_flash_mmap_pages((const int *) a4, n3, (spi_flash_mmap_memory_t) n2, \
                                (const void **) a1, (spi_flash_mmap_handle_t *) a0); NIPn(4)) \
  YV(spi_flash, spi_flash_munmap, spi_flash_munmap((spi_flash_mmap_handle_t) a0); DROP) \
  YV(spi_flash, spi_flash_mmap_dump, spi_flash_mmap_dump()) \
  YV(spi_flash, spi_flash_mmap_get_free_pages, \
      n0 = spi_flash_mmap_get_free_pages((spi_flash_mmap_memory_t) n0)) \
  YV(spi_flash, spi_flash_cache2phys, n0 = spi_flash_cache2phys(a0)) \
  YV(spi_flash, spi_flash_phys2cache, \
      n0 = (cell_t) spi_flash_phys2cache(n1, (spi_flash_mmap_memory_t) n0); NIP) \
  YV(spi_flash, spi_flash_cache_enabled, PUSH spi_flash_cache_enabled()) \
  YV(spi_flash, esp_partition_find, \
      n0 = (cell_t) esp_partition_find((esp_partition_type_t) n2, \
                                       (esp_partition_subtype_t) n1, c0); NIPn(2)) \
  YV(spi_flash, esp_partition_find_first, \
      n0 = (cell_t) esp_partition_find_first((esp_partition_type_t) n2, \
                                             (esp_partition_subtype_t) n1, c0); NIPn(2)) \
  YV(spi_flash, esp_partition_t_size, PUSH sizeof(esp_partition_t)) \
  YV(spi_flash, esp_partition_get, \
      n0 = (cell_t) esp_partition_get((esp_partition_iterator_t) a0)) \
  YV(spi_flash, esp_partition_next, \
      n0 = (cell_t) esp_partition_next((esp_partition_iterator_t) a0)) \
  YV(spi_flash, esp_partition_iterator_release, \
      esp_partition_iterator_release((esp_partition_iterator_t) a0); DROP) \
  YV(spi_flash, esp_partition_verify, n0 = (cell_t) esp_partition_verify((esp_partition_t *) a0)) \
  YV(spi_flash, esp_partition_read, \
      n0 = esp_partition_read((const esp_partition_t *) a3, n2, a1, n0); NIPn(3)) \
  YV(spi_flash, esp_partition_write, \
      n0 = esp_partition_write((const esp_partition_t *) a3, n2, a1, n0); NIPn(3)) \
  YV(spi_flash, esp_partition_erase_range, \
      n0 = esp_partition_erase_range((const esp_partition_t *) a2, n1, n0); NIPn(2)) \
  YV(spi_flash, esp_partition_mmap, \
      n0 = esp_partition_mmap((const esp_partition_t *) a5, n4, n3, \
                              (spi_flash_mmap_memory_t) n2, \
                              (const void **) a1, \
                              (spi_flash_mmap_handle_t *) a0); NIPn(5)) \
  YV(spi_flash, esp_partition_get_sha256, \
      n0 = esp_partition_get_sha256((const esp_partition_t *) a1, b0); NIP) \
  YV(spi_flash, esp_partition_check_identity, \
      n0 = esp_partition_check_identity((const esp_partition_t *) a1, \
                                        (const esp_partition_t *) a0); NIP)
#endif

#ifndef ENABLE_SPIFFS_SUPPORT
// Provide a default failing SPIFFS.begin
# define OPTIONAL_SPIFFS_SUPPORT \
  X("SPIFFS.begin", SPIFFS_BEGIN, NIPn(2); n0 = 0)
#else
# ifndef SIM_PRINT_ONLY
#  include "SPIFFS.h"
# endif
# define OPTIONAL_SPIFFS_SUPPORT \
  XV(SPIFFS, "SPIFFS.begin", SPIFFS_BEGIN, \
      tos = SPIFFS.begin(n2, c1, n0); NIPn(2)) \
  XV(SPIFFS, "SPIFFS.end", SPIFFS_END, SPIFFS.end()) \
  XV(SPIFFS, "SPIFFS.format", SPIFFS_FORMAT, PUSH SPIFFS.format()) \
  XV(SPIFFS, "SPIFFS.totalBytes", SPIFFS_TOTAL_BYTES, PUSH SPIFFS.totalBytes()) \
  XV(SPIFFS, "SPIFFS.usedBytes", SPIFFS_USED_BYTES, PUSH SPIFFS.usedBytes())
#endif

#ifndef ENABLE_FREERTOS_SUPPORT
# define OPTIONAL_FREERTOS_SUPPORT
#else
# ifndef SIM_PRINT_ONLY
#  include "freertos/FreeRTOS.h"
#  include "freertos/task.h"
# endif
# define OPTIONAL_FREERTOS_SUPPORT \
  YV(rtos, vTaskDelete, vTaskDelete((TaskHandle_t) n0); DROP) \
  YV(rtos, xTaskCreatePinnedToCore, n0 = xTaskCreatePinnedToCore((TaskFunction_t) a6, \
        c5, n4, a3, (UBaseType_t) n2, (TaskHandle_t *) a1, (BaseType_t) n0); NIPn(6)) \
  YV(rtos, xPortGetCoreID, PUSH xPortGetCoreID())
#endif

#ifndef ENABLE_INTERRUPTS_SUPPORT
# define OPTIONAL_INTERRUPTS_SUPPORT
#else
# ifndef SIM_PRINT_ONLY
#  include "esp_intr_alloc.h"
#  include "driver/timer.h"
#  include "driver/gpio.h"
static cell_t EspIntrAlloc(cell_t source, cell_t flags, cell_t xt, cell_t arg, void *ret);
static cell_t GpioIsrHandlerAdd(cell_t pin, cell_t xt, cell_t arg);
static cell_t TimerIsrRegister(cell_t group, cell_t timer, cell_t xt, cell_t arg, cell_t flags, void *ret);
# endif
# define OPTIONAL_INTERRUPTS_SUPPORT \
  YV(interrupts, gpio_config, n0 = gpio_config((const gpio_config_t *) a0)) \
  YV(interrupts, gpio_reset_pin, n0 = gpio_reset_pin((gpio_num_t) n0)) \
  YV(interrupts, gpio_set_intr_type, n0 = gpio_set_intr_type((gpio_num_t) n1, (gpio_int_type_t) n0); NIP) \
  YV(interrupts, gpio_intr_enable, n0 = gpio_intr_enable((gpio_num_t) n0)) \
  YV(interrupts, gpio_intr_disable, n0 = gpio_intr_disable((gpio_num_t) n0)) \
  YV(interrupts, gpio_set_level, n0 = gpio_set_level((gpio_num_t) n1, n0); NIP) \
  YV(interrupts, gpio_get_level, n0 = gpio_get_level((gpio_num_t) n0)) \
  YV(interrupts, gpio_set_direction, n0 = gpio_set_direction((gpio_num_t) n1, (gpio_mode_t) n0); NIP) \
  YV(interrupts, gpio_set_pull_mode, n0 = gpio_set_pull_mode((gpio_num_t) n1, (gpio_pull_mode_t) n0); NIP) \
  YV(interrupts, gpio_wakeup_enable, n0 = gpio_wakeup_enable((gpio_num_t) n1, (gpio_int_type_t) n0); NIP) \
  YV(interrupts, gpio_wakeup_disable, n0 = gpio_wakeup_disable((gpio_num_t) n0)) \
  YV(interrupts, gpio_pullup_en, n0 = gpio_pullup_en((gpio_num_t) n0)) \
  YV(interrupts, gpio_pullup_dis, n0 = gpio_pullup_dis((gpio_num_t) n0)) \
  YV(interrupts, gpio_pulldown_en, n0 = gpio_pulldown_en((gpio_num_t) n0)) \
  YV(interrupts, gpio_pulldown_dis, n0 = gpio_pulldown_dis((gpio_num_t) n0)) \
  YV(interrupts, gpio_hold_en, n0 = gpio_hold_en((gpio_num_t) n0)) \
  YV(interrupts, gpio_hold_dis, n0 = gpio_hold_dis((gpio_num_t) n0)) \
  YV(interrupts, gpio_deep_sleep_hold_en, gpio_deep_sleep_hold_en()) \
  YV(interrupts, gpio_deep_sleep_hold_dis, gpio_deep_sleep_hold_dis()) \
  YV(interrupts, gpio_install_isr_service, n0 = gpio_install_isr_service(n0)) \
  YV(interrupts, gpio_uninstall_isr_service, gpio_uninstall_isr_service()) \
  YV(interrupts, gpio_isr_handler_add, n0 = GpioIsrHandlerAdd(n2, n1, n0); NIPn(2)) \
  YV(interrupts, gpio_isr_handler_remove, n0 = gpio_isr_handler_remove((gpio_num_t) n0)) \
  YV(interrupts, gpio_set_drive_capability, n0 = gpio_set_drive_capability((gpio_num_t) n1, (gpio_drive_cap_t) n0); NIP) \
  YV(interrupts, gpio_get_drive_capability, n0 = gpio_get_drive_capability((gpio_num_t) n1, (gpio_drive_cap_t *) a0); NIP) \
  YV(interrupts, esp_intr_alloc, n0 = EspIntrAlloc(n4, n3, n2, n1, a0); NIPn(4)) \
  YV(interrupts, esp_intr_free, n0 = esp_intr_free((intr_handle_t) n0)) \
  YV(timers, timer_isr_register, n0 = TimerIsrRegister(n5, n4, n3, n2, n1, a0); NIPn(5))
#endif

#ifndef ENABLE_RMT_SUPPORT
# define OPTIONAL_RMT_SUPPORT
#else
# ifndef SIM_PRINT_ONLY
#  include "driver/rmt.h"
# endif
# define OPTIONAL_RMT_SUPPORT \
  YV(rmt, rmt_set_clk_div, n0 = rmt_set_clk_div((rmt_channel_t) n1, n0); NIP) \
  YV(rmt, rmt_get_clk_div, n0 = rmt_get_clk_div((rmt_channel_t) n1, b0); NIP) \
  YV(rmt, rmt_set_rx_idle_thresh, n0 = rmt_set_rx_idle_thresh((rmt_channel_t) n1, n0); NIP) \
  YV(rmt, rmt_get_rx_idle_thresh, \
    n0 = rmt_get_rx_idle_thresh((rmt_channel_t) n1, (uint16_t *) a0); NIP) \
  YV(rmt, rmt_set_mem_block_num, n0 = rmt_set_mem_block_num((rmt_channel_t) n1, n0); NIP) \
  YV(rmt, rmt_get_mem_block_num, n0 = rmt_get_mem_block_num((rmt_channel_t) n1, b0); NIP) \
  YV(rmt, rmt_set_tx_carrier, n0 = rmt_set_tx_carrier((rmt_channel_t) n4, n3, n2, n1, \
                                                (rmt_carrier_level_t) n0); NIPn(4)) \
  YV(rmt, rmt_set_mem_pd, n0 = rmt_set_mem_pd((rmt_channel_t) n1, n0); NIP) \
  YV(rmt, rmt_get_mem_pd, n0 = rmt_get_mem_pd((rmt_channel_t) n1, (bool *) a0); NIP) \
  YV(rmt, rmt_tx_start, n0 = rmt_tx_start((rmt_channel_t) n1, n0); NIP) \
  YV(rmt, rmt_tx_stop, n0 = rmt_tx_stop((rmt_channel_t) n0)) \
  YV(rmt, rmt_rx_start, n0 = rmt_rx_start((rmt_channel_t) n1, n0); NIP) \
  YV(rmt, rmt_rx_stop, n0 = rmt_rx_stop((rmt_channel_t) n0)) \
  YV(rmt, rmt_tx_memory_reset, n0 = rmt_tx_memory_reset((rmt_channel_t) n0)) \
  YV(rmt, rmt_rx_memory_reset, n0 = rmt_rx_memory_reset((rmt_channel_t) n0)) \
  YV(rmt, rmt_set_memory_owner, n0 = rmt_set_memory_owner((rmt_channel_t) n1, (rmt_mem_owner_t) n0); NIP) \
  YV(rmt, rmt_get_memory_owner, n0 = rmt_get_memory_owner((rmt_channel_t) n1, (rmt_mem_owner_t *) a0); NIP) \
  YV(rmt, rmt_set_tx_loop_mode, n0 = rmt_set_tx_loop_mode((rmt_channel_t) n1, n0); NIP) \
  YV(rmt, rmt_get_tx_loop_mode, n0 = rmt_get_tx_loop_mode((rmt_channel_t) n1, (bool *) a0); NIP) \
  YV(rmt, rmt_set_rx_filter, n0 = rmt_set_rx_filter((rmt_channel_t) n2, n1, n0); NIPn(2)) \
  YV(rmt, rmt_set_source_clk, n0 = rmt_set_source_clk((rmt_channel_t) n1, (rmt_source_clk_t) n0); NIP) \
  YV(rmt, rmt_get_source_clk, n0 = rmt_get_source_clk((rmt_channel_t) n1, (rmt_source_clk_t * ) a0); NIP) \
  YV(rmt, rmt_set_idle_level, n0 = rmt_set_idle_level((rmt_channel_t) n2, n1, \
        (rmt_idle_level_t) n0); NIPn(2)) \
  YV(rmt, rmt_get_idle_level, n0 = rmt_get_idle_level((rmt_channel_t) n2, \
        (bool *) a1, (rmt_idle_level_t *) a0); NIPn(2)) \
  YV(rmt, rmt_get_status, n0 = rmt_get_status((rmt_channel_t) n1, (uint32_t *) a0); NIP) \
  YV(rmt, rmt_set_rx_intr_en, n0 = rmt_set_rx_intr_en((rmt_channel_t) n1, n0); NIP) \
  YV(rmt, rmt_set_err_intr_en, n0 = rmt_set_err_intr_en((rmt_channel_t) n1, (rmt_mode_t) n0); NIP) \
  YV(rmt, rmt_set_tx_intr_en, n0 = rmt_set_tx_intr_en((rmt_channel_t) n1, n0); NIP) \
  YV(rmt, rmt_set_tx_thr_intr_en, n0 = rmt_set_tx_thr_intr_en((rmt_channel_t) n2, n1, n0); NIPn(2)) \
  YV(rmt, rmt_set_gpio, n0 = rmt_set_gpio((rmt_channel_t) n3, (rmt_mode_t) n2, (gpio_num_t) n1, n0); NIPn(3)) \
  YV(rmt, rmt_config, n0 = rmt_config((const rmt_config_t *) a0)) \
  YV(rmt, rmt_isr_register, n0 = rmt_isr_register((void (*)(void*)) a3, a2, n1, \
        (rmt_isr_handle_t *) a0); NIPn(3)) \
  YV(rmt, rmt_isr_deregister, n0 = rmt_isr_deregister((rmt_isr_handle_t) n0)) \
  YV(rmt, rmt_fill_tx_items, n0 = rmt_fill_tx_items((rmt_channel_t) n3, \
        (rmt_item32_t *) a2, n1, n0); NIPn(3)) \
  YV(rmt, rmt_driver_install, n0 = rmt_driver_install((rmt_channel_t) n2, n1, n0); NIPn(2)) \
  YV(rmt, rmt_driver_uinstall, n0 = rmt_driver_uninstall((rmt_channel_t) n0)) \
  YV(rmt, rmt_get_channel_status, n0 = rmt_get_channel_status((rmt_channel_status_result_t *) a0)) \
  YV(rmt, rmt_get_counter_clock, n0 = rmt_get_counter_clock((rmt_channel_t) n1, (uint32_t *) a0); NIP) \
  YV(rmt, rmt_write_items, n0 = rmt_write_items((rmt_channel_t) n3, (rmt_item32_t *) a2, n1, n0); NIPn(3)) \
  YV(rmt, rmt_wait_tx_done, n0 = rmt_wait_tx_done((rmt_channel_t) n1, n0); NIP) \
  YV(rmt, rmt_get_ringbuf_handle, n0 = rmt_get_ringbuf_handle((rmt_channel_t) n1, (RingbufHandle_t *) a0); NIP) \
  YV(rmt, rmt_translator_init, n0 = rmt_translator_init((rmt_channel_t) n1, (sample_to_rmt_t) n0); NIP) \
  YV(rmt, rmt_translator_set_context, n0 = rmt_translator_set_context((rmt_channel_t) n1, a0); NIP) \
  YV(rmt, rmt_translator_get_context, n0 = rmt_translator_get_context((const size_t *) a1, (void **) a0); NIP) \
  YV(rmt, rmt_write_sample, n0 = rmt_write_sample((rmt_channel_t) n3, b2, n1, n0); NIPn(3))
#endif

#ifndef ENABLE_CAMERA_SUPPORT
# define OPTIONAL_CAMERA_SUPPORT
#else
# ifndef SIM_PRINT_ONLY
#  include "esp_camera.h"
# endif
# define OPTIONAL_CAMERA_SUPPORT \
  YV(camera, esp_camera_init, n0 = esp_camera_init((camera_config_t *) a0)) \
  YV(camera, esp_camera_deinit, PUSH esp_camera_deinit()) \
  YV(camera, esp_camera_fb_get, PUSH esp_camera_fb_get()) \
  YV(camera, esp_camera_fb_return, esp_camera_fb_return((camera_fb_t *) a0); DROP) \
  YV(camera, esp_camera_sensor_get, PUSH esp_camera_sensor_get()) \
  YV(camera, esp_camera_save_to_nvs, n0 = esp_camera_save_to_nvs(c0)) \
  YV(camera, esp_camera_load_from_nvs, n0 = esp_camera_load_from_nvs(c0))
#endif

#ifndef ENABLE_SOCKETS_SUPPORT
# define OPTIONAL_SOCKETS_SUPPORT
#else
# ifndef SIM_PRINT_ONLY
#  include <errno.h>
#  include <netdb.h>
#  include <sys/select.h>
#  include <sys/socket.h>
#  include <sys/time.h>
#  include <sys/types.h>
#  include <sys/un.h>
#  include <sys/poll.h>
# endif
# define OPTIONAL_SOCKETS_SUPPORT \
  YV(sockets, socket, n0 = socket(n2, n1, n0); NIPn(2)) \
  YV(sockets, setsockopt, n0 = setsockopt(n4, n3, n2, a1, n0); NIPn(4)) \
  YV(sockets, bind, n0 = bind(n2, (struct sockaddr *) a1, n0); NIPn(2)) \
  YV(sockets, listen, n0 = listen(n1, n0); NIP) \
  YV(sockets, connect, n0 = connect(n2, (struct sockaddr *) a1, n0); NIPn(2)) \
  YV(sockets, sockaccept, n0 = accept(n2, (struct sockaddr *) a1, (socklen_t *) a0); NIPn(2)) \
  YV(sockets, select, n0 = select(n4, (fd_set *) a3, (fd_set *) a2, (fd_set *) a1, (struct timeval *) a0); NIPn(4)) \
  YV(sockets, poll, n0 = poll((struct pollfd *) a2, (nfds_t) n1, n0); NIPn(2)) \
  YV(sockets, send, n0 = send(n3, a2, n1, n0); NIPn(3)) \
  YV(sockets, sendto, n0 = sendto(n5, a4, n3, n2, (const struct sockaddr *) a1, n0); NIPn(5)) \
  YV(sockets, sendmsg, n0 = sendmsg(n2, (const struct msghdr *) a1, n0); NIPn(2)) \
  YV(sockets, recv, n0 = recv(n3, a2, n1, n0); NIPn(3)) \
  YV(sockets, recvfrom, n0 = recvfrom(n5, a4, n3, n2, (struct sockaddr *) a1, (socklen_t *) a0); NIPn(5)) \
  YV(sockets, recvmsg, n0 = recvmsg(n2, (struct msghdr *) a1, n0); NIPn(2)) \
  YV(sockets, gethostbyname, n0 = (cell_t) gethostbyname(c0)) \
  XV(sockets, "errno", ERRNO, PUSH errno)
#endif

#ifndef ENABLE_SD_SUPPORT
# define OPTIONAL_SD_SUPPORT
#else
# ifndef SIM_PRINT_ONLY
#  include "SD.h"
# endif
# define OPTIONAL_SD_SUPPORT \
  XV(SD, "SD.begin", SD_BEGIN, PUSH SD.begin()) \
  XV(SD, "SD.beginFull", SD_BEGIN_FULL, \
      tos = SD.begin(n5, *(SPIClass*)a4, n3, c2, n1, n0); NIPn(5)) \
  XV(SD, "SD.beginDefaults", SD_BEGIN_DEFAULTS, \
      PUSH SS; PUSH &SPI; PUSH 4000000; PUSH "/sd"; PUSH 5; PUSH false) \
  XV(SD, "SD.end", SD_END, SD.end()) \
  XV(SD, "SD.cardType", SD_CARD_TYPE, PUSH SD.cardType()) \
  XV(SD, "SD.totalBytes", SD_TOTAL_BYTES, PUSH SD.totalBytes()) \
  XV(SD, "SD.usedBytes", SD_USED_BYTES, PUSH SD.usedBytes())
#endif

#ifndef ENABLE_SD_MMC_SUPPORT
# define OPTIONAL_SD_MMC_SUPPORT
#else
# ifndef SIM_PRINT_ONLY
#  include "SD_MMC.h"
# endif
# define OPTIONAL_SD_MMC_SUPPORT \
  XV(SD_MMC, "SD_MMC.begin", SD_MMC_BEGIN, PUSH SD_MMC.begin()) \
  XV(SD_MMC, "SD_MMC.beginFull", SD_MMC_BEGIN_FULL, tos = SD_MMC.begin(c2, n1, n0); NIPn(2)) \
  XV(SD_MMC, "SD_MMC.beginDefaults", SD_MMC_BEGIN_DEFAULTS, \
      PUSH "/sdcard"; PUSH false; PUSH false) \
  XV(SD_MMC, "SD_MMC.end", SD_MMC_END, SD_MMC.end()) \
  XV(SD_MMC, "SD_MMC.cardType", SD_MMC_CARD_TYPE, PUSH SD_MMC.cardType()) \
  XV(SD_MMC, "SD_MMC.totalBytes", SD_MMC_TOTAL_BYTES, PUSH SD_MMC.totalBytes()) \
  XV(SD_MMC, "SD_MMC.usedBytes", SD_MMC_USED_BYTES, PUSH SD_MMC.usedBytes())
#endif

#ifndef ENABLE_I2C_SUPPORT
# define OPTIONAL_I2C_SUPPORT
#else
# ifndef SIM_PRINT_ONLY
#  include <Wire.h>
# endif
# define OPTIONAL_I2C_SUPPORT \
  XV(Wire, "Wire.begin", WIRE_BEGIN, n0 = Wire.begin(n1, n0); NIP) \
  XV(Wire, "Wire.setClock", WIRE_SET_CLOCK, Wire.setClock(n0); DROP) \
  XV(Wire, "Wire.getClock", WIRE_GET_CLOCK, PUSH Wire.getClock()) \
  XV(Wire, "Wire.setTimeout", WIRE_SET_TIMEOUT, Wire.setTimeout(n0); DROP) \
  XV(Wire, "Wire.getTimeout", WIRE_GET_TIMEOUT, PUSH Wire.getTimeout()) \
  XV(Wire, "Wire.beginTransmission", WIRE_BEGIN_TRANSMISSION, Wire.beginTransmission(n0); DROP) \
  XV(Wire, "Wire.endTransmission", WIRE_END_TRANSMISSION, SET Wire.endTransmission(n0)) \
  XV(Wire, "Wire.requestFrom", WIRE_REQUEST_FROM, n0 = Wire.requestFrom(n2, n1, n0); NIPn(2)) \
  XV(Wire, "Wire.write", WIRE_WRITE, n0 = Wire.write(b1, n0); NIP) \
  XV(Wire, "Wire.available", WIRE_AVAILABLE, PUSH Wire.available()) \
  XV(Wire, "Wire.read", WIRE_READ, PUSH Wire.read()) \
  XV(Wire, "Wire.peek", WIRE_PEEK, PUSH Wire.peek()) \
  XV(Wire, "Wire.flush", WIRE_FLUSH, Wire.flush())
#endif

#ifndef ENABLE_SERIAL_BLUETOOTH_SUPPORT
# define OPTIONAL_SERIAL_BLUETOOTH_SUPPORT
#else
# ifndef SIM_PRINT_ONLY
#  include "esp_bt_device.h"
#  include "BluetoothSerial.h"
#  define bt0 ((BluetoothSerial *) a0)
# endif
# define OPTIONAL_SERIAL_BLUETOOTH_SUPPORT \
  XV(bluetooth, "SerialBT.new", SERIALBT_NEW, PUSH new BluetoothSerial()) \
  XV(bluetooth, "SerialBT.delete", SERIALBT_DELETE, delete bt0; DROP) \
  XV(bluetooth, "SerialBT.begin", SERIALBT_BEGIN, n0 = bt0->begin(c2, n1); NIPn(2)) \
  XV(bluetooth, "SerialBT.end", SERIALBT_END, bt0->end(); DROP) \
  XV(bluetooth, "SerialBT.available", SERIALBT_AVAILABLE, n0 = bt0->available()) \
  XV(bluetooth, "SerialBT.readBytes", SERIALBT_READ_BYTES, n0 = bt0->readBytes(b2, n1); NIPn(2)) \
  XV(bluetooth, "SerialBT.write", SERIALBT_WRITE, n0 = bt0->write(b2, n1); NIPn(2)) \
  XV(bluetooth, "SerialBT.flush", SERIALBT_FLUSH, bt0->flush(); DROP) \
  XV(bluetooth, "SerialBT.hasClient", SERIALBT_HAS_CLIENT, n0 = bt0->hasClient()) \
  XV(bluetooth, "SerialBT.enableSSP", SERIALBT_ENABLE_SSP, bt0->enableSSP(); DROP) \
  XV(bluetooth, "SerialBT.setPin", SERIALBT_SET_PIN, n0 = bt0->setPin(c1); NIP) \
  XV(bluetooth, "SerialBT.unpairDevice", SERIALBT_UNPAIR_DEVICE, \
      n0 = bt0->unpairDevice(b1); NIP) \
  XV(bluetooth, "SerialBT.connect", SERIALBT_CONNECT, n0 = bt0->connect(c1); NIP) \
  XV(bluetooth, "SerialBT.connectAddr", SERIALBT_CONNECT_ADDR, n0 = bt0->connect(b1); NIP) \
  XV(bluetooth, "SerialBT.disconnect", SERIALBT_DISCONNECT, n0 = bt0->disconnect()) \
  XV(bluetooth, "SerialBT.connected", SERIALBT_CONNECTED, n0 = bt0->connected(n1); NIP) \
  XV(bluetooth, "SerialBT.isReady", SERIALBT_IS_READY, n0 = bt0->isReady(n2, n1); NIPn(2)) \
  /* Bluetooth */ \
  YV(bluetooth, esp_bt_dev_get_address, PUSH esp_bt_dev_get_address())
#endif

#ifndef ENABLE_WIFI_SUPPORT
# define OPTIONAL_WIFI_SUPPORT
#else
# ifndef SIM_PRINT_ONLY
#  include <WiFi.h>
#  include <WiFiClient.h>

static IPAddress ToIP(cell_t ip) {
  return IPAddress(ip & 0xff, ((ip >> 8) & 0xff), ((ip >> 16) & 0xff), ((ip >> 24) & 0xff));
}

static cell_t FromIP(IPAddress ip) {
  cell_t ret = 0;
  ret = (ret << 8) | ip[3];
  ret = (ret << 8) | ip[2];
  ret = (ret << 8) | ip[1];
  ret = (ret << 8) | ip[0];
  return ret;
}
# endif

# define OPTIONAL_WIFI_SUPPORT \
  /* WiFi */ \
  XV(WiFi, "WiFi.config", WIFI_CONFIG, \
      WiFi.config(ToIP(n3), ToIP(n2), ToIP(n1), ToIP(n0)); DROPn(4)) \
  XV(WiFi, "WiFi.begin", WIFI_BEGIN, WiFi.begin(c1, c0); DROPn(2)) \
  XV(WiFi, "WiFi.disconnect", WIFI_DISCONNECT, WiFi.disconnect()) \
  XV(WiFi, "WiFi.status", WIFI_STATUS, PUSH WiFi.status()) \
  XV(WiFi, "WiFi.macAddress", WIFI_MAC_ADDRESS, WiFi.macAddress(b0); DROP) \
  XV(WiFi, "WiFi.localIP", WIFI_LOCAL_IPS, PUSH FromIP(WiFi.localIP())) \
  XV(WiFi, "WiFi.mode", WIFI_MODE, WiFi.mode((wifi_mode_t) n0); DROP) \
  XV(WiFi, "WiFi.setTxPower", WIFI_SET_TX_POWER, WiFi.setTxPower((wifi_power_t) n0); DROP) \
  XV(WiFi, "WiFi.getTxPower", WIFI_GET_TX_POWER, PUSH WiFi.getTxPower()) \
  XV(WiFi, "WiFi.softAP", WIFI_SOFTAP, n0 = WiFi.softAP(c1, c0); NIP) \
  XV(WiFi, "WiFi.softAPIP", WIFI_SOFTAP_IP, PUSH FromIP(WiFi.softAPIP())) \
  XV(WiFi, "WiFi.softAPBroadcastIP", WIFI_SOFTAP_BROADCASTIP, PUSH FromIP(WiFi.softAPBroadcastIP())) \
  XV(WiFi, "WiFi.softAPNetworkID", WIFI_SOFTAP_NETWORKID, PUSH FromIP(WiFi.softAPNetworkID())) \
  XV(WiFi, "WiFi.softAPConfig", WIFI_SOFTAP_CONFIG, n0 = WiFi.softAPConfig(ToIP(n2), ToIP(n1), ToIP(n0))) \
  XV(WiFi, "WiFi.softAPdisconnect", WIFI_SOFTAP_DISCONNECT, n0 = WiFi.softAPdisconnect(n0)) \
  XV(WiFi, "WiFi.softAPgetStationNum", WIFI_SOFTAP_GET_STATION_NUM, PUSH WiFi.softAPgetStationNum())
#endif

#ifndef ENABLE_MDNS_SUPPORT
# define OPTIONAL_MDNS_SUPPORT
#else
# ifndef SIM_PRINT_ONLY
#  include <ESPmDNS.h>
# endif
# define OPTIONAL_MDNS_SUPPORT \
  /* mDNS */ \
  X("MDNS.begin", MDNS_BEGIN, n0 = MDNS.begin(c0))
#endif

#ifndef ENABLE_OLED_SUPPORT
# define OPTIONAL_OLED_SUPPORT
#else
# ifndef SIM_PRINT_ONLY
#  include <Adafruit_GFX.h>
#  include <Adafruit_SSD1306.h>
static Adafruit_SSD1306 *oled_display = 0;
# endif
# define OPTIONAL_OLED_SUPPORT \
  YV(oled, OledAddr, PUSH &oled_display) \
  YV(oled, OledNew, oled_display = new Adafruit_SSD1306(n2, n1, &Wire, n0); DROPn(3)) \
  YV(oled, OledDelete, delete oled_display) \
  YV(oled, OledBegin, n0 = oled_display->begin(n1, n0); NIP) \
  YV(oled, OledHOME, oled_display->setCursor(0,0); DROP) \
  YV(oled, OledCLS, oled_display->clearDisplay()) \
  YV(oled, OledTextc, oled_display->setTextColor(n0); DROP) \
  YV(oled, OledPrintln, oled_display->println(c0); DROP) \
  YV(oled, OledNumln, oled_display->println(n0); DROP) \
  YV(oled, OledNum, oled_display->print(n0); DROP) \
  YV(oled, OledDisplay, oled_display->display()) \
  YV(oled, OledPrint, oled_display->write(c0); DROP) \
  YV(oled, OledInvert, oled_display->invertDisplay(n0); DROP) \
  YV(oled, OledTextsize, oled_display->setTextSize(n0); DROP) \
  YV(oled, OledSetCursor, oled_display->setCursor(n1,n0); DROPn(2)) \
  YV(oled, OledPixel, oled_display->drawPixel(n2, n1, n0); DROPn(2)) \
  YV(oled, OledDrawL, oled_display->drawLine(n4, n3, n2, n1, n0); DROPn(4)) \
  YV(oled, OledCirc, oled_display->drawCircle(n3,n2, n1, n0); DROPn(3)) \
  YV(oled, OledCircF, oled_display->fillCircle(n3, n2, n1, n0); DROPn(3)) \
  YV(oled, OledRect, oled_display->drawRect(n4, n3, n2, n1, n0); DROPn(4)) \
  YV(oled, OledRectF, oled_display->fillRect(n4, n3, n2, n1, n0); DROPn(3)) \
  YV(oled, OledRectR, oled_display->drawRoundRect(n5, n4, n3, n2, n1, n0); DROPn(5)) \
  YV(oled, OledRectRF, oled_display->fillRoundRect(n5, n4, n3, n2, n1, n0 ); DROPn(5))
#endif
static char filename[PATH_MAX];
static char filename2[PATH_MAX];

#define IMMEDIATE 1
#define SMUDGE 2
#define BUILTIN_FORK 4
#define BUILTIN_MARK 8

typedef struct {
  cell_t *heap, **current, ***context;
  cell_t *latestxt, notfound;
  cell_t *heap_start;
  cell_t heap_size, stack_cells;
  const char *boot;
  cell_t boot_size;
  const char *tib;
  cell_t ntib, tin, state, base;
  int argc;
  char **argv;
  cell_t *(*runner)(cell_t *rp);  // pointer to forth_run

  // Layout not used by Forth.
  cell_t *rp;  // spot to park main thread
  cell_t DOLIT_XT, DOFLIT_XT, DOEXIT_XT, YIELD_XT;
  void *DOCREATE_OP;
  const BUILTIN_WORD *builtins;
} G_SYS;
#define PRINT_ERRORS 0

#define CELL_MASK (sizeof(cell_t) - 1)
#define CELL_LEN(n) (((n) + CELL_MASK) / sizeof(cell_t))
#define FIND(name) find((name), sizeof(name) - 1)
#define UPPER(ch) (((ch) >= 'a' && (ch) <= 'z') ? ((ch) & 0x5F) : (ch))
#define CELL_ALIGNED(a) ((((cell_t) (a)) + CELL_MASK) & ~CELL_MASK)
#define IMMEDIATE 1
#define SMUDGE 2
#define BUILTIN_FORK 4
#define BUILTIN_MARK 8

// Maximum ALSO layers.
#define VOCABULARY_DEPTH 16

#if PRINT_ERRORS
#include <stdio.h>
#endif

enum {
#define V(name) VOC_ ## name,
  VOCABULARY_LIST
#undef V
};

enum {
#define V(name) VOC_ ## name ## _immediate = VOC_ ## name + (IMMEDIATE << 8),
  VOCABULARY_LIST
#undef V
};

static G_SYS *g_sys = 0;

static cell_t convert(const char *pos, cell_t n, cell_t base, cell_t *ret) {
  *ret = 0;
  cell_t negate = 0;
  if (!n) { return 0; }
  if (*pos == '-') { negate = -1; ++pos; --n; }
  if (*pos == '$') { base = 16; ++pos; --n; }
  for (; n; --n) {
    uintptr_t d = UPPER(*pos) - '0';
    if (d > 9) {
      d -= 7;
      if (d < 10) { return 0; }
    }
    if (d >= (uintptr_t) base) { return 0; }
    *ret = *ret * base + d;
    ++pos;
  }
  if (negate) { *ret = -*ret; }
  return -1;
}

static cell_t fconvert(const char *pos, cell_t n, float *ret) {
  *ret = 0;
  cell_t negate = 0;
  cell_t has_dot = 0;
  cell_t exp = 0;
  float shift = 1.0;
  if (!n) { return 0; }
  if (*pos == '-') { negate = -1; ++pos; --n; }
  for (; n; --n) {
    if (*pos >= '0' && *pos <= '9') {
      if (has_dot) {
        shift = shift * 0.1f;
        *ret = *ret + (*pos - '0') * shift;
      } else {
        *ret = *ret * 10 + (*pos - '0');
      }
    } else if (*pos == 'e' || *pos == 'E') {
      break;
    } else if (*pos == '.') {
      if (has_dot) { return 0; }
      has_dot = -1;
    } else {
      return 0;
    }
    ++pos;
  }
  if (!n) { return 0; }  // must have E
  ++pos; --n;
  if (n) {
    if (!convert(pos, n, 10, &exp)) { return 0; }
  }
  if (exp < -128 || exp > 128) { return 0; }
  for (; exp < 0; ++exp) { *ret *= 0.1f; }
  for (; exp > 0; --exp) { *ret *= 10.0f; }
  if (negate) { *ret = -*ret; }
  return -1;
}

static cell_t same(const char *a, const char *b, cell_t len) {
  for (;len && UPPER(*a) == UPPER(*b); --len, ++a, ++b);
  return len == 0;
}

static cell_t find(const char *name, cell_t len) {
  for (cell_t ***voc = g_sys->context; *voc; ++voc) {
    cell_t xt = (cell_t) **voc;
    while (xt) {
      if ((*TOFLAGS(xt) & BUILTIN_FORK)) {
        cell_t vocab = TOLINK(xt)[3];
        for (int i = 0; g_sys->builtins[i].name; ++i) {
          if (g_sys->builtins[i].vocabulary == vocab &&
              len == g_sys->builtins[i].name_length &&
              same(name, g_sys->builtins[i].name, len)) {
            return (cell_t) &g_sys->builtins[i].code;
          }
        }
      }
      if (!(*TOFLAGS(xt) & SMUDGE) &&
          len == *TONAMELEN(xt) &&
          same(name, TONAME(xt), len)) {
        return xt;
      }
      xt = *TOLINK(xt);
    }
  }
  return 0;
}

static void finish(void) {
  if (g_sys->latestxt && !*TOPARAMS(g_sys->latestxt)) {
    cell_t sz = g_sys->heap - &g_sys->latestxt[1];
    if (sz < 0 || sz > 0xffff) { sz = 0xffff; }
    *TOPARAMS(g_sys->latestxt) = sz;
  }
}

static void create(const char *name, cell_t nlength, cell_t flags, void *op) {
  finish();
  g_sys->heap = (cell_t *) CELL_ALIGNED(g_sys->heap);
  for (cell_t n = nlength; n; --n) { CCOMMA(*name++); }  // name
  g_sys->heap = (cell_t *) CELL_ALIGNED(g_sys->heap);
  COMMA(*g_sys->current);  // link
  COMMA((nlength << 8) | flags);  // flags & length
  *g_sys->current = g_sys->heap;
  g_sys->latestxt = g_sys->heap;
  COMMA(op);  // code
}

static int match(char sep, char ch) {
  return sep == ch || (sep == ' ' && (ch == '\t' || ch == '\n' || ch == '\r'));
}

static cell_t parse(cell_t sep, cell_t *ret) {
  if (sep == ' ') {
    while (g_sys->tin < g_sys->ntib &&
           match(sep, g_sys->tib[g_sys->tin])) { ++g_sys->tin; }
  }
  cell_t start = g_sys->tin;
  while (g_sys->tin < g_sys->ntib &&
         !match(sep, g_sys->tib[g_sys->tin])) { ++g_sys->tin; }
  cell_t len = g_sys->tin - start;
  if (g_sys->tin < g_sys->ntib) { ++g_sys->tin; }
  *ret = (cell_t) (g_sys->tib + start);
  return len;
}

static cell_t *evaluate1(cell_t *rp) {
  cell_t call = 0;
  cell_t tos, *sp, *ip;
  float *fp;
  UNPARK;
  cell_t name;
  cell_t len = parse(' ', &name);
  if (len == 0) { DUP; tos = 0; PARK; return rp; }  // ignore empty
  cell_t xt = find((const char *) name, len);
  if (xt) {
    if (g_sys->state && !(*TOFLAGS(xt) & IMMEDIATE)) {
      COMMA(xt);
    } else {
      call = xt;
    }
  } else {
    cell_t n;
    if (convert((const char *) name, len, g_sys->base, &n)) {
      if (g_sys->state) {
        COMMA(g_sys->DOLIT_XT);
        COMMA(n);
      } else {
        PUSH n;
      }
    } else {
      float f;
      if (fconvert((const char *) name, len, &f)) {
        if (g_sys->state) {
          COMMA(g_sys->DOFLIT_XT);
          *(float *) g_sys->heap++ = f;
        } else {
          *++fp = f;
        }
      } else {
#if PRINT_ERRORS
        fprintf(stderr, "CANT FIND: ");
        fwrite((void *) name, 1, len, stderr);
        fprintf(stderr, "\n");
#endif
        PUSH name;
        PUSH len;
        PUSH -1;
        call = g_sys->notfound;
      }
    }
  }
  PUSH call;
  PARK;
  return rp;
}

static cell_t *forth_run(cell_t *initrp);

static void forth_init(int argc, char *argv[],
                       void *heap, cell_t heap_size,
                       const char *src, cell_t src_len) {
  g_sys = (G_SYS *) heap;
  memset(g_sys, 0, sizeof(G_SYS));
  g_sys->heap_start = (cell_t *) heap;
  g_sys->heap_size = heap_size;
  g_sys->stack_cells = STACK_CELLS;

  // Start heap after G_SYS area.
  g_sys->heap = g_sys->heap_start + sizeof(G_SYS) / sizeof(cell_t);
  g_sys->heap += 4;  // Leave a little room.

  // Allocate stacks.
  float *fp = (float *) (g_sys->heap + 1); g_sys->heap += STACK_CELLS;
  cell_t *rp = g_sys->heap + 1; g_sys->heap += STACK_CELLS;
  cell_t *sp = g_sys->heap + 1; g_sys->heap += STACK_CELLS;

  // FORTH worldlist (relocated when vocabularies added).
  cell_t *forth_wordlist = g_sys->heap;
  COMMA(0);
  // Vocabulary stack.
  g_sys->current = (cell_t **) forth_wordlist;
  g_sys->context = (cell_t ***) g_sys->heap;
  g_sys->latestxt = 0;
  COMMA(forth_wordlist);
  for (int i = 0; i < VOCABULARY_DEPTH; ++i) { COMMA(0); }

  // Setup boot text.
  g_sys->boot = src;
  g_sys->boot_size = src_len;

  forth_run(0);
#define V(name) \
  create(#name "-builtins", sizeof(#name "-builtins") - 1, \
      BUILTIN_FORK, g_sys->DOCREATE_OP); \
  COMMA(VOC_ ## name);
  VOCABULARY_LIST
#undef V
  g_sys->latestxt = 0;  // So last builtin doesn't get wrong size.
  g_sys->DOLIT_XT = FIND("DOLIT");
  g_sys->DOFLIT_XT = FIND("DOFLIT");
  g_sys->DOEXIT_XT = FIND("EXIT");
  g_sys->YIELD_XT = FIND("YIELD");
  g_sys->notfound = FIND("DROP");

  // Init code.
  cell_t *start = g_sys->heap;
  COMMA(FIND("EVALUATE1"));
  COMMA(FIND("BRANCH"));
  COMMA(start);

  g_sys->argc = argc;
  g_sys->argv = argv;
  g_sys->base = 10;
  g_sys->tib = src;
  g_sys->ntib = src_len;

  *++rp = (cell_t) fp;
  *++rp = (cell_t) sp;
  *++rp = (cell_t) start;
  g_sys->rp = rp;
  g_sys->runner = forth_run;
}
#define JMPW goto **(void **) w
#define NEXT w = *ip++; JMPW
#define ADDROF(x) (&& OP_ ## x)

static cell_t *forth_run(cell_t *init_rp) {
  static const BUILTIN_WORD builtins[] = {
#define Z(flags, name, op, code) \
    name, ((VOC_ ## flags >> 8) & 0xff) | BUILTIN_MARK, \
    sizeof(name) - 1, (VOC_ ## flags & 0xff), && OP_ ## op,
    PLATFORM_OPCODE_LIST
    TIER2_OPCODE_LIST
    TIER1_OPCODE_LIST
    TIER0_OPCODE_LIST
#undef Z
    0, 0, 0, 0, 0,
  };

  if (!init_rp) {
    g_sys->DOCREATE_OP = ADDROF(DOCREATE);
    g_sys->builtins = builtins;
    return 0;
  }
  register cell_t *ip, *rp, *sp, tos, w;
  register float *fp, ft;
  rp = init_rp; UNPARK; NEXT;
#define Z(flags, name, op, code) OP_ ## op: { code; } NEXT;
  PLATFORM_OPCODE_LIST
  TIER2_OPCODE_LIST
  TIER1_OPCODE_LIST
  TIER0_OPCODE_LIST
#undef Z
}
const char boot[] = R"""(
: (   41 parse drop drop ; immediate
: \   10 parse drop drop ; immediate
: #!   10 parse drop drop ; immediate  ( shebang for scripts )
( Now can do comments! )
( Stack Baseline )
sp@ constant sp0
rp@ constant rp0
fp@ constant fp0
: depth ( -- n ) sp@ sp0 - cell/ ;
: fdepth ( -- n ) fp@ fp0 - 4 / ;

( Useful heap size words )
: remaining ( -- n ) 'heap-start @ 'heap-size @ + 'heap @ - ;
: used ( -- n ) 'heap @ sp@ 'stack-cells @ cells + - 28 + ;

( Quoting Words )
: ' bl parse 2dup find dup >r -rot r> 0= 'notfound @ execute 2drop ;
: ['] ' aliteral ; immediate
: char bl parse drop c@ ;
: [char] char aliteral ; immediate

( Core Control Flow )
create BEGIN ' nop @ ' begin !        : begin   ['] begin , here ; immediate
create AGAIN ' branch @ ' again !     : again   ['] again , , ; immediate
create UNTIL ' 0branch @ ' until !    : until   ['] until , , ; immediate
create AHEAD ' branch @ ' ahead !     : ahead   ['] ahead , here 0 , ; immediate
create THEN ' nop @ ' then !          : then   ['] then , here swap ! ; immediate
create IF ' 0branch @ ' if !          : if   ['] if , here 0 , ; immediate
create ELSE ' branch @ ' else !       : else   ['] else , here 0 , swap here swap ! ; immediate
create WHILE ' 0branch @ ' while !    : while   ['] while , here 0 , swap ; immediate
create REPEAT ' branch @ ' repeat !   : repeat   ['] repeat , , here swap ! ; immediate
create AFT ' branch @ ' aft !         : aft   drop ['] aft , here 0 , here swap ; immediate

( Recursion )
: recurse   current @ @ aliteral ['] execute , ; immediate

( Postpone - done here so we have ['] and IF )
: immediate? ( xt -- f ) >flags 1 and 0= 0= ;
: postpone ' dup immediate? if , else aliteral ['] , , then ; immediate

( Rstack nest depth )
variable nest-depth

( FOR..NEXT )
create FOR ' >r @ ' for !         : for   1 nest-depth +! ['] for , here ; immediate
create NEXT ' donext @ ' next !   : next   -1 nest-depth +! ['] next , , ; immediate

( DO..LOOP )
variable leaving
: leaving,   here leaving @ , leaving ! ;
: leaving(   leaving @ 0 leaving !   2 nest-depth +! ;
: )leaving   leaving @ swap leaving !  -2 nest-depth +!
             begin dup while dup @ swap here swap ! repeat drop ;
: DO ( n n -- .. ) swap r> -rot >r >r >r ;
: do ( lim s -- ) leaving( postpone DO here ; immediate
: ?DO ( n n -- n n f .. )
   2dup = if 2drop r> @ >r else swap r> cell+ -rot >r >r >r then ;
: ?do ( lim s -- ) leaving( postpone ?DO leaving, here ; immediate
: UNLOOP   r> rdrop rdrop >r ;
: LEAVE   r> rdrop rdrop @ >r ;
: leave   postpone LEAVE leaving, ; immediate
: +LOOP ( n -- ) dup 0< swap r> r> rot + dup r@ < -rot >r >r xor 0=
                 if r> cell+ rdrop rdrop >r else r> @ >r then ;
: +loop ( n -- ) postpone +LOOP , )leaving ; immediate
: LOOP   r> r> 1+ dup r@ < -rot >r >r 0=
         if r> cell+ rdrop rdrop >r else r> @ >r then ;
: loop   postpone LOOP , )leaving ; immediate
create I ' r@ @ ' i !  ( i is same as r@ )
: J ( -- n ) rp@ 3 cells - @ ;
: K ( -- n ) rp@ 5 cells - @ ;

( Exceptions )
variable handler
: catch ( xt -- n )
  fp@ >r sp@ >r handler @ >r rp@ handler ! execute
  r> handler ! rdrop rdrop 0 ;
: throw ( n -- )
  dup if handler @ rp! r> handler !
         r> swap >r sp! drop r> r> fp! else drop then ;
' throw 'notfound !

( Values )
: value ( n -- ) constant ;
: value-bind ( xt-val xt )
   >r >body state @ if
     r@ ['] ! = if rdrop ['] doset , , else aliteral r> , then
   else r> execute then ;
: to ( n -- ) ' ['] ! value-bind ; immediate
: +to ( n -- ) ' ['] +! value-bind ; immediate

( Deferred Words )
: defer ( "name" -- ) create 0 , does> @ dup 0= throw execute ;
: is ( xt "name -- ) postpone to ; immediate
( Defer I/O to platform specific )
defer type
defer key
defer key?
defer bye
: emit ( n -- ) >r rp@ 1 type rdrop ;
: space bl emit ;   : cr 13 emit nl emit ;

( Numeric Output )
variable hld
: pad ( -- a ) here 80 + ;
: digit ( u -- c ) 9 over < 7 and + 48 + ;
: extract ( n base -- n c ) u/mod swap digit ;
: <# ( -- ) pad hld ! ;
: hold ( c -- ) hld @ 1 - dup hld ! c! ;
: # ( u -- u ) base @ extract hold ;
: #s ( u -- 0 ) begin # dup while repeat ;
: sign ( n -- ) 0< if 45 hold then ;
: #> ( w -- b u ) drop hld @ pad over - ;
: str ( n -- b u ) dup >r abs <# #s r> sign #> ;
: hex ( -- ) 16 base ! ;   : octal ( -- ) 8 base ! ;
: decimal ( -- ) 10 base ! ;   : binary ( -- ) 2 base ! ;
: u. ( u -- ) <# #s #> type space ;
: . ( w -- ) base @ 10 xor if u. exit then str type space ;
: ? ( a -- ) @ . ;
: n. ( n -- ) base @ swap decimal <# #s #> type base ! ;

( Strings )
: parse-quote ( -- a n ) [char] " parse ;
: $place ( a n -- ) for aft dup c@ c, 1+ then next drop ;
: zplace ( a n -- ) $place 0 c, align ;
: $@   r@ dup cell+ swap @ r> dup @ 1+ aligned + cell+ >r ;
: s"   parse-quote state @ if postpone $@ dup , zplace
       else dup here swap >r >r zplace r> r> then ; immediate
: ."   postpone s" state @ if postpone type else type then ; immediate
: z"   postpone s" state @ if postpone drop else drop then ; immediate
: r"   parse-quote state @ if swap aliteral aliteral then ; immediate
: r|   [char] | parse state @ if swap aliteral aliteral then ; immediate
: r~   [char] ~ parse state @ if swap aliteral aliteral then ; immediate
: s>z ( a n -- z ) here >r zplace r> ;
: z>s ( z -- a n ) 0 over begin dup c@ while 1+ swap 1+ swap repeat drop ;

( Better Errors )
: notfound ( a n n -- )
   if cr ." ERROR: " type ."  NOT FOUND!" cr -1 throw then ;
' notfound 'notfound !

( Input )
: raw.s   depth 0 max for aft sp@ r@ cells - @ . then next ;
variable echo -1 echo !   variable arrow -1 arrow !  0 value wascr
: *emit ( n -- ) dup 13 = if drop cr else emit then ;
: ?echo ( n -- ) echo @ if *emit else drop then ;
: ?arrow.   arrow @ if >r >r raw.s r> r> ." --> " then ;
: *key ( -- n )
  begin
    key
    dup nl = if
      drop wascr if 0 else 13 exit then
    then
    dup 13 = to wascr
    dup if exit else drop then
  again ;
: eat-till-cr   begin *key dup 13 = if ?echo exit else drop then again ;
: accept ( a n -- n ) ?arrow. 0 swap begin 2dup < while
     *key
     dup 13 = if ?echo drop nip exit then
     dup 8 = over 127 = or if
       drop over if rot 1- rot 1- rot 8 ?echo bl ?echo 8 ?echo then
     else
       dup ?echo
       >r rot r> over c! 1+ -rot swap 1+ swap
     then
   repeat drop nip
   eat-till-cr
;
200 constant input-limit
: tib ( -- a ) 'tib @ ;
create input-buffer   input-limit allot
: tib-setup   input-buffer 'tib ! ;
: refill   tib-setup tib input-limit accept #tib ! 0 >in ! -1 ;

( Stack Guards )
sp0 'stack-cells @ 2 3 */ cells + constant sp-limit
: ?stack   sp@ sp0 < if ." STACK UNDERFLOW " -1 throw then
           sp-limit sp@ < if ." STACK OVERFLOW " -1 throw then ;

( REPL )
: prompt   ."  ok" cr ;
: evaluate-buffer   begin >in @ #tib @ < while evaluate1 ?stack repeat ;
: evaluate ( a n -- ) 'tib @ >r #tib @ >r >in @ >r
                      #tib ! 'tib ! 0 >in ! evaluate-buffer
                      r> >in ! r> #tib ! r> 'tib ! ;
: quit    begin ['] evaluate-buffer catch
          if 0 state ! sp0 sp! fp0 fp! rp0 rp! ." ERROR" cr then
          prompt refill drop again ;
variable boot-prompt
: free. ( nf nu -- ) 2dup swap . ." free + " . ." used = " 2dup + . ." total ("
                     over + 100 -rot */ n. ." % free)" ;
: raw-ok   ."  v7.0.7.4 - rev 595ed9e8296c64661139" cr
           boot-prompt @ if boot-prompt @ execute then
           ." Forth dictionary: " remaining used free. cr
           ." 3 x Forth stacks: " 'stack-cells @ cells . ." bytes each" cr
           prompt refill drop quit ;
( Interpret time conditionals )

: DEFINED? ( "name" -- xt|0 )
   bl parse find state @ if aliteral then ; immediate
defer [SKIP]
: [THEN] ;   : [ELSE] [SKIP] ;   : [IF] 0= if [SKIP] then ;
: [SKIP]' 0 begin postpone defined? dup if
    dup ['] [IF] = if swap 1+ swap then
    dup ['] [ELSE] = if swap dup 0 <= if 2drop exit then swap then
    dup ['] [THEN] = if swap 1- dup 0< if 2drop exit then swap then
  then drop again ;
' [SKIP]' is [SKIP]
( Implement Vocabularies )
( normal: link, flags&len, code )
( vocab:  link, flags&len, code | link , len=0, voclink )
variable last-vocabulary
: vocabulary ( "name" )
  create current @ 2 cells + , 0 , last-vocabulary @ ,
  current @ @ last-vocabulary !
  does> context ! ;
: definitions   context @ current ! ;
vocabulary FORTH
' forth >body @ >link ' forth >body !
forth definitions

( Make it easy to transfer words between vocabularies )
: xt-find& ( xt -- xt& ) context @ begin 2dup @ <> while @ >link& repeat nip ;
: xt-hide ( xt -- ) xt-find& dup @ >link swap ! ;
8 constant BUILTIN_MARK
: xt-transfer ( xt --  ) dup >flags BUILTIN_MARK and if drop exit then
  dup xt-hide   current @ @ over >link& !   current @ ! ;
: transfer ( "name" ) ' xt-transfer ;
: }transfer ;
: transfer{ begin ' dup ['] }transfer = if drop exit then xt-transfer again ;

( Watered down versions of these )
: only   forth 0 context cell+ ! ;
: voc-stack-end ( -- a ) context begin dup @ while cell+ repeat ;
: also   context context cell+ voc-stack-end over - 2 cells + cmove> ;
: previous
  voc-stack-end context cell+ = throw
  context cell+ context voc-stack-end over - cell+ cmove ;
: sealed   0 last-vocabulary @ >body ! ;

( Hide some words in an internals vocabulary )
vocabulary internals   internals definitions

( Vocabulary chain for current scope, place at the -1 position )
variable scope   scope context cell - !

transfer{
  xt-find& xt-hide xt-transfer
  voc-stack-end last-vocabulary notfound
  *key *emit wascr eat-till-cr
  immediate? input-buffer ?echo ?arrow. arrow
  evaluate-buffer aliteral value-bind
  leaving( )leaving leaving leaving,
  parse-quote digit $@ raw.s
  tib-setup input-limit sp-limit ?stack
  [SKIP] [SKIP]' raw-ok boot-prompt free.
  $place zplace BUILTIN_MARK
}transfer

( Move branching opcodes to separate vocabulary )
vocabulary internalized  internalized definitions
: cleave   ' >link xt-transfer ;
cleave begin   cleave again   cleave until
cleave ahead   cleave then    cleave if
cleave else    cleave while   cleave repeat
cleave aft     cleave for     cleave next
cleave do      cleave ?do     cleave +loop
cleave loop    cleave leave

forth definitions

( Make DOES> switch to compile mode when interpreted )
(
forth definitions internals
' does>
: does>   state @ if postpone does> exit then
          ['] constant @ current @ @ dup >r !
          here r> cell+ ! postpone ] ; immediate
xt-hide
forth definitions
)
: sf, ( r -- ) here sf! sfloat allot ;

: afliteral ( r -- ) ['] DOFLIT , sf, align ;
: fliteral   afliteral ; immediate

: fconstant ( r "name" ) create sf, align does> sf@ ;
: fvariable ( "name" ) create sfloat allot align ;

6 value precision
: set-precision ( n -- ) to precision ;

internals definitions
: #f+s ( r -- ) fdup precision for aft 10e f* then next
                precision for aft fdup f>s 10 mod [char] 0 + hold 0.1e f* then next
                [char] . hold fdrop f>s #s ;
forth definitions internals

: #fs ( r -- ) fdup f0< if fnegate #f+s [char] - hold else #f+s then ;
: f. ( r -- ) <# #fs #> type space ;
: f.s   ." <" fdepth n. ." > "
        fdepth 0 max for aft fp@ r@ sfloats - sf@ f. then next ;

forth definitions
( Vocabulary for building C-style structures )

vocabulary structures   structures definitions

variable last-align
: typer ( align sz "name" ) create , ,
                            does> dup cell+ @ last-align ! @ ;
1 1 typer i8
2 2 typer i16
4 4 typer i32
cell 8 typer i64
cell cell typer ptr
long-size long-size typer long

variable last-struct

: struct ( "name" ) 1 0 typer latestxt >body last-struct ! ;
: align-by ( a n -- a ) 1- dup >r + r> invert and ;
: struct-align ( n -- )
  dup last-struct @ cell+ @ max last-struct @ cell+ !
  last-struct @ @ swap align-by last-struct @ ! ;
: field ( n "name" )
  last-align @ struct-align
  create last-struct @ @ , last-struct @ +!
  does> @ + ;

forth definitions
( Words with OS assist )
: allocate ( n -- a ior ) malloc dup 0= ;
: free ( a -- ior ) sysfree 0 ;
: resize ( a n -- a ior ) realloc dup 0= ;

( Migrate various words to separate vocabularies, and constants )

forth definitions internals
: read-dir ( dh -- a n ) readdir dup if z>s else 0 then ;
forth definitions

vocabulary ESP   ESP definitions
transfer ESP-builtins
only forth definitions

vocabulary Wire   Wire definitions
transfer wire-builtins
forth definitions

vocabulary WiFi   WiFi definitions
transfer WiFi-builtins
( WiFi Modes )
0 constant WIFI_MODE_NULL
1 constant WIFI_MODE_STA
2 constant WIFI_MODE_AP
3 constant WIFI_MODE_APSTA
forth definitions

vocabulary SD   SD definitions
transfer SD-builtins
forth definitions

vocabulary SD_MMC   SD_MMC definitions
transfer SD_MMC-builtins
forth definitions

vocabulary spi_flash   spi_flash definitions
transfer spi_flash-builtins
DEFINED? spi_flash_init [IF]
0 constant SPI_PARTITION_TYPE_APP
1 constant SPI_PARTITION_TYPE_DATA
$ff constant SPI_PARTITION_SUBTYPE_ANY

also structures
struct esp_partition_t
  ( Work around changing struct layout )
  esp_partition_t_size 40 >= [IF]
    ptr field p>gap
  [THEN]
  ptr field p>type
  ptr field p>subtype
  ptr field p>address
  ptr field p>size
  ptr field p>label

: p. ( part -- )
  base @ >r >r decimal
  ." TYPE: " r@ p>type @ . ." SUBTYPE: " r@ p>subtype @ .
  ." ADDR: " r@ hex p>address @ .  ." SIZE: " r@ p>size @ .
  ." LABEL: " r> p>label @ z>s type cr r> base ! ;
: list-partition-type ( type -- )
  SPI_PARTITION_SUBTYPE_ANY 0 esp_partition_find
  begin dup esp_partition_get p. esp_partition_next dup 0= until drop ;
: list-partitions   SPI_PARTITION_TYPE_APP list-partition-type
                    SPI_PARTITION_TYPE_DATA list-partition-type ;
[THEN]
only forth definitions

vocabulary SPIFFS   SPIFFS definitions
transfer SPIFFS-builtins
forth definitions

vocabulary ledc  ledc definitions
transfer ledc-builtins
forth definitions

vocabulary Serial   Serial definitions
transfer Serial-builtins
forth definitions

vocabulary sockets   sockets definitions
transfer sockets-builtins
1 constant SOCK_STREAM
2 constant SOCK_DGRAM
3 constant SOCK_RAW

2 constant AF_INET
16 constant sizeof(sockaddr_in)
1 constant SOL_SOCKET
2 constant SO_REUSEADDR

: bs, ( n -- ) dup 8 rshift c, c, ;
: s, ( n -- ) dup c, 8 rshift c, ;
: l, ( n -- ) dup s, 16 rshift s, ;
: sockaddr   create 16 c, AF_INET c, 0 bs, 0 l, 0 l, 0 l, ;
: ->port@ ( a -- n ) 2 + >r r@ c@ 8 lshift r> 1+ c@ + ;
: ->port! ( n a --  ) 2 + >r dup 8 rshift r@ c! r> 1+ c! ;
: ->addr@ ( a -- n ) 4 + ul@ ;
: ->addr! ( n a --  ) 4 + l! ;
: ->h_addr ( hostent -- n ) 2 cells + 8 + @ @ ul@ ;
: ip# ( n -- n ) dup 255 and n. [char] . emit 8 rshift ;
: ip. ( n -- ) ip# ip# ip# 255 and n. ;
forth definitions

vocabulary interrupts   interrupts definitions
transfer interrupts-builtins
DEFINED? gpio_config [IF]
0 constant ESP_INTR_FLAG_DEFAULT
: ESP_INTR_FLAG_LEVELn ( n=1-6 -- n ) 1 swap lshift ;
1 7 lshift constant ESP_INTR_FLAG_NMI
1 8 lshift constant ESP_INTR_FLAG_SHARED
1 9 lshift constant ESP_INTR_FLAG_EDGE
1 10 lshift constant ESP_INTR_FLAG_IRAM
1 11 lshift constant ESP_INTR_FLAG_INTRDISABLED
( Prefix these with # because GPIO_INTR_DISABLE conflicts with a function. )
0 constant #GPIO_INTR_DISABLE
1 constant #GPIO_INTR_POSEDGE
2 constant #GPIO_INTR_NEGEDGE
3 constant #GPIO_INTR_ANYEDGE
4 constant #GPIO_INTR_LOW_LEVEL
5 constant #GPIO_INTR_HIGH_LEVEL
( Easy word to trigger on any change to a pin )
ESP_INTR_FLAG_DEFAULT gpio_install_isr_service drop
: pinchange ( xt pin ) dup #GPIO_INTR_ANYEDGE gpio_set_intr_type throw
                       swap 0 gpio_isr_handler_add throw ;
[THEN]
forth definitions

vocabulary rmt   rmt definitions
transfer rmt-builtins
forth definitions

vocabulary rtos   rtos definitions
transfer rtos-builtins
forth definitions

vocabulary bluetooth   bluetooth definitions
transfer bluetooth-builtins
forth definitions

vocabulary oled   oled definitions
transfer oled-builtins
DEFINED? OledNew [IF]
128 constant WIDTH
64 constant HEIGHT
-1 constant OledReset
0 constant BLACK
1 constant WHITE
1 constant SSD1306_EXTERNALVCC
2 constant SSD1306_SWITCHCAPVCC
: OledInit
  OledAddr @ 0= if
    WIDTH HEIGHT OledReset OledNew
    SSD1306_SWITCHCAPVCC $3C OledBegin drop
  then
  OledCLS
  2 OledTextsize  ( Draw 2x Scale Text )
  WHITE OledTextc  ( Draw white text )
  0 0 OledSetCursor  ( Start at top-left corner )
  z" *Esp32forth*" OledPrintln OledDisplay
;
[THEN]
forth definitions

internals definitions
( Heap Capabilities )
1 0 lshift constant MALLOC_CAP_EXEC
1 1 lshift constant MALLOC_CAP_32BIT
1 2 lshift constant MALLOC_CAP_8BIT
1 3 lshift constant MALLOC_CAP_DMA
1 10 lshift constant MALLOC_CAP_SPIRAM
1 11 lshift constant MALLOC_CAP_INTERNAL
1 12 lshift constant MALLOC_CAP_DEFAULT
1 13 lshift constant MALLOC_CAP_IRAM_8BIT
1 14 lshift constant MALLOC_CAP_RETENTION
1 15 lshift constant MALLOC_CAP_RTCRAM
forth definitions
( Words built after boot )

( For tests and asserts )
: assert ( f -- ) 0= throw ;

( Print spaces )
: spaces ( n -- ) for aft space then next ;

internals definitions

( Temporary for platforms without CALLCODE )
DEFINED? CALLCODE 0= [IF]
  create CALLCODE
[THEN]

( Safe memory access, i.e. aligned )
cell 1- constant cell-mask
: cell-base ( a -- a ) cell-mask invert and ;
: cell-shift ( a -- a ) cell-mask and 8 * ;
: ca@ ( a -- n ) dup cell-base @ swap cell-shift rshift 255 and ;

( Print address line leaving room )
: dump-line ( a -- a ) cr <# #s #> 20 over - >r type r> spaces ;

( Semi-dangerous word to trim down the system heap )
DEFINED? realloc [IF]
: relinquish ( n -- ) negate 'heap-size +! 'heap-start @ 'heap-size @ realloc drop ;
[THEN]

forth definitions internals

( Examine Memory )
: dump ( a n -- )
   over 15 and if over dump-line over 15 and 3 * spaces then
   for aft
     dup 15 and 0= if dup dump-line then
     dup ca@ <# # #s #> type space 1+
   then next drop cr ;

( Remove from Dictionary )
: forget ( "name" ) ' dup >link current @ !  >name drop here - allot ;

internals definitions
1 constant IMMEDIATE_MARK
2 constant SMUDGE
4 constant BUILTIN_FORK
16 constant NONAMED
32 constant +TAB
64 constant -TAB
128 constant ARGS_MARK
: mem= ( a a n -- f)
   for aft 2dup c@ swap c@ <> if 2drop rdrop 0 exit then 1+ swap 1+ then next 2drop -1 ;
forth definitions also internals
: :noname ( -- xt ) 0 , current @ @ , NONAMED SMUDGE or ,
                    here dup current @ ! ['] mem= @ , postpone ] ;
: str= ( a n a n -- f) >r swap r@ <> if rdrop 2drop 0 exit then r> mem= ;
: startswith? ( a n a n -- f ) >r swap r@ < if rdrop 2drop 0 exit then r> mem= ;
: .s   ." <" depth n. ." > " raw.s cr ;
only forth definitions

( Tweak indent on branches )
internals internalized definitions

: flags'or! ( n -- ) ' >flags& dup >r c@ or r> c! ;
+TAB flags'or! BEGIN
-TAB flags'or! AGAIN
-TAB flags'or! UNTIL
+TAB flags'or! AHEAD
-TAB flags'or! THEN
+TAB flags'or! IF
+TAB -TAB or flags'or! ELSE
+TAB -TAB or flags'or! WHILE
-TAB flags'or! REPEAT
+TAB flags'or! AFT
+TAB flags'or! FOR
-TAB flags'or! NEXT
+TAB flags'or! DO
ARGS_MARK +TAB or flags'or! ?DO
ARGS_MARK -TAB or flags'or! +LOOP
ARGS_MARK -TAB or flags'or! LOOP
ARGS_MARK flags'or! LEAVE

forth definitions 

( Definitions building to SEE and ORDER )
internals definitions
variable indent
: see. ( xt -- ) >name type space ;
: icr   cr indent @ 0 max 4* spaces ;
: indent+! ( n -- ) indent +! icr ;
: see-one ( xt -- xt+1 )
   dup cell+ swap @
   dup ['] DOLIT = if drop dup @ . cell+ exit then
   dup ['] DOSET = if drop ." TO " dup @ cell - see. cell+ icr exit then
   dup ['] DOFLIT = if drop dup sf@ <# [char] e hold #fs #> type space cell+ exit then
   dup ['] $@ = if drop ['] s" see.
                   dup @ dup >r >r dup cell+ r> type cell+ r> 1+ aligned +
                   [char] " emit space exit then
   dup ['] DOES> = if icr then
   dup >flags -TAB AND if -1 indent+! then
   dup see.
   dup >flags +TAB AND if
     1 indent+!
   else
     dup >flags -TAB AND if icr then
   then
   dup ['] ! = if icr then
   dup ['] +! = if icr then
   dup  @ ['] BRANCH @ =
   over @ ['] 0BRANCH @ = or
   over @ ['] DONEXT @ = or
   over >flags ARGS_MARK and or
       if swap cell+ swap then
   drop
;
: see-loop   dup >body swap >params 1- cells over +
             begin 2dup < while swap see-one swap repeat 2drop ;
: ?see-flags   >flags IMMEDIATE_MARK and if ." IMMEDIATE " then ;
: see-xt ( xt -- )
  dup @ ['] see-loop @ = if
    ['] : see.  dup see.
    1 indent ! icr
    dup see-loop
    -1 indent+! ['] ; see.
    ?see-flags cr
    exit
  then
  dup >flags BUILTIN_FORK and if ." Built-in-fork: " see. exit then
  dup @ ['] input-buffer @ = if ." CREATE/VARIABLE: " see. cr exit then
  dup @ ['] SMUDGE @ = if ." DOES>/CONSTANT: " see. cr exit then
  dup @ ['] callcode @ = if ." Code: " see. cr exit then
  dup >params 0= if ." Built-in: " see. cr exit then
  ." Unsupported: " see. cr ;

: nonvoc? ( xt -- f )
  dup 0= if exit then dup >name nip swap >flags NONAMED BUILTIN_FORK or and or ;
: see-vocabulary ( voc )
  @ begin dup nonvoc? while dup see-xt >link repeat drop cr ;
: >vocnext ( xt -- xt ) >body 2 cells + @ ;
: see-all
  last-vocabulary @ begin dup while
    ." VOCABULARY " dup see. cr ." ------------------------" cr
    dup >body see-vocabulary
    >vocnext
  repeat drop cr ;
: voclist-from ( voc -- ) begin dup while dup see. cr >vocnext repeat drop ;
: voclist   last-vocabulary @ voclist-from ;
: voc. ( voc -- ) 2 cells - see. ;
: vocs. ( voc -- ) dup voc. @ begin dup while
    dup nonvoc? 0= if ." >> " dup 2 cells - voc. then
    >link
  repeat drop cr ;

( Words to measure size of things )
: size-vocabulary ( voc )
  @ begin dup nonvoc? while
    dup >params . dup >size . dup . dup see. cr >link
  repeat drop ;
: size-all
  last-vocabulary @ begin dup while
    0 . 0 . 0 . dup see. cr
    dup >body size-vocabulary
    >vocnext
  repeat drop cr ;

forth definitions also internals
: see   ' see-xt ;
: order   context begin dup @ while dup @ vocs. cell+ repeat drop ;
only forth definitions

( List words in Dictionary / Vocabulary )
internals definitions
70 value line-width
0 value line-pos
: onlines ( xt -- xt )
   line-pos line-width > if cr 0 to line-pos then
   dup >name nip 1+ line-pos + to line-pos ;
: vins. ( voc -- )
  >r 'builtins begin dup >link while
    dup >params r@ = if dup onlines see. then
    3 cells +
  repeat drop rdrop ;
: ins. ( n xt -- n ) cell+ @ vins. ;
: ?ins. ( xt -- xt ) dup >flags BUILTIN_FORK and if dup ins. then ;
forth definitions also internals
: vlist   0 to line-pos context @ @
          begin dup nonvoc? while ?ins. dup onlines see. >link repeat drop cr ;
: words   0 to line-pos context @ @
          begin dup while ?ins. dup onlines see. >link repeat drop cr ;
only forth definitions
( Lazy loaded code words )
: asm r|

also forth definitions
vocabulary asm
internals definitions

: ca! ( n a -- ) dup cell-base >r cell-shift swap over lshift
                 swap 255 swap lshift invert r@ @ and or r> ! ;

also asm definitions

variable code-start
variable code-at

DEFINED? posix [IF]
also posix
: reserve ( n -- )
  0 swap PROT_READ PROT_WRITE PROT_EXEC or or
  MAP_ANONYMOUS MAP_PRIVATE or -1 0 mmap code-start ! ;
previous
4096 reserve
[THEN]

DEFINED? esp [IF]
also esp
: reserve ( n -- ) MALLOC_CAP_EXEC heap_caps_malloc code-start ! ;
previous
1024 reserve
[THEN]

code-start @ code-at !

: chere ( -- a ) code-at @ ;
: callot ( n -- ) code-at +! ;
: code1, ( n -- ) chere ca! 1 callot ;
: code2, ( n -- ) dup code1, 8 rshift code1, ;
: code3, ( n -- ) dup code2, 16 rshift code1, ;
: code4, ( n -- ) dup code2, 16 rshift code2, ;
cell 8 = [IF]
: code,  dup code4, 32 rshift code4, ;
[ELSE]
: code,  code4, ;
[THEN]
: end-code   previous ;

also forth definitions

: code ( "name" ) create ['] callcode @ latestxt !
                  code-at @ latestxt cell+ ! also asm ;

previous previous previous
asm

| evaluate ;
( Local Variables )

( NOTE: These are not yet gforth compatible )

internals definitions

( Leave a region for locals definitions )
1024 constant locals-capacity  128 constant locals-gap
create locals-area locals-capacity allot
variable locals-here  locals-area locals-here !
: <>locals   locals-here @ here locals-here ! here - allot ;

: local@ ( n -- ) rp@ + @ ;
: local! ( n -- ) rp@ + ! ;
: local+! ( n -- ) rp@ + +! ;

variable scope-depth
variable local-op   ' local@ local-op !
: scope-exit   scope-depth @ for aft postpone rdrop then next ;
: scope-clear
   scope-exit
   scope-depth @ negate nest-depth +!
   0 scope-depth !   0 scope !   locals-area locals-here ! ;
: do-local ( n -- ) nest-depth @ + cells negate aliteral
                    local-op @ ,  ['] local@ local-op ! ;
: scope-create ( a n -- )
   dup >r $place align ( name )
   scope @ , r> 8 lshift 1 or , ( IMMEDIATE ) here scope ! ( link, flags&length )
   ['] scope-clear @ ( docol) ,
   nest-depth @ negate aliteral postpone do-local ['] exit ,
   1 scope-depth +!  1 nest-depth +!
;

: ?room   locals-here @ locals-area - locals-capacity locals-gap - >
          if scope-clear -1 throw then ;

: }? ( a n -- ) 1 <> if drop 0 exit then c@ [char] } = ;
: --? ( a n -- ) s" --" str= ;
: (to) ( xt -- ) ['] local! local-op ! execute ;
: (+to) ( xt -- ) ['] local+! local-op ! execute ;

also forth definitions

: (local) ( a n -- )
   dup 0= if 2drop exit then 
   ?room <>locals scope-create <>locals postpone >r ;
: {   bl parse
      dup 0= if scope-clear -1 throw then
      2dup --? if 2drop [char] } parse 2drop exit then
      2dup }? if 2drop exit then
      recurse (local) ; immediate
( TODO: Hide the words overriden here. )
: ;   scope-clear postpone ; ; immediate
: exit   scope-exit postpone exit ; immediate
: to ( n -- ) ' dup >flags if (to) else ['] ! value-bind then ; immediate
: +to ( n -- ) ' dup >flags if (+to) else ['] +! value-bind then ; immediate

only forth definitions
internals definitions
variable cases
forth definitions internals

: CASE ( n -- ) cases @  0 cases ! ; immediate
: ENDCASE   postpone drop cases @ for aft postpone then then next
            cases ! ; immediate
: OF ( n -- ) postpone over postpone = postpone if postpone drop ; immediate
: ENDOF   1 cases +! postpone else ; immediate

forth definitions
( Cooperative Tasks )

vocabulary tasks   tasks definitions also internals

variable task-list

: .tasks   task-list @ begin dup 2 cells - see. @ dup task-list @ = until drop ;

forth definitions tasks also internals

: pause
  rp@ sp@ task-list @ cell+ !
  task-list @ @ task-list !
  task-list @ cell+ @ sp! rp!
;

: task ( xt dsz rsz "name" )
   create here >r 0 , 0 , ( link, sp )
   swap here cell+ r@ cell+ ! cells allot
   here r@ cell+ @ ! cells allot
   dup 0= if drop else
     here r@ cell+ @ @ ! ( set rp to point here )
     , postpone pause ['] branch , here 3 cells - ,
   then rdrop ;

: start-task ( t -- )
   task-list @ if
     task-list @ @ over !
     task-list @ !
   else
     dup task-list !
     dup !
   then
;

DEFINED? ms-ticks [IF]
  : ms ( n -- ) ms-ticks >r begin pause ms-ticks r@ - over >= until rdrop drop ;
[THEN]

tasks definitions
0 0 0 task main-task   main-task start-task
only forth definitions
( Byte Stream / Ring Buffer )

vocabulary streams   streams definitions

: stream ( n "name" ) create 1+ dup , 0 , 0 , allot align ;
: >write ( st -- wr ) cell+ ;   : >read ( st -- rd ) 2 cells + ;
: >offset ( n st -- a ) 3 cells + + ;
: stream# ( sz -- n ) >r r@ >write @ r@ >read @ - r> @ mod ;
: full? ( st -- f ) dup stream# swap @ 1- = ;
: empty? ( st -- f ) stream# 0= ;
: wait-write ( st -- ) begin dup full? while pause repeat drop ;
: wait-read ( st -- ) begin dup empty? while pause repeat drop ;
: ch>stream ( ch st -- )
   dup wait-write
   >r r@ >write @ r@ >offset c!
   r@ >write @ 1+ r@ @ mod r> >write ! ;
: stream>ch ( st -- ch )
   dup wait-read
   >r r@ >read @ r@ >offset c@
   r@ >read @ 1+ r@ @ mod r> >read ! ;
: >stream ( a n st -- )
   swap for aft over c@ over ch>stream swap 1+ swap then next 2drop ;
: stream> ( a n st -- )
   begin over 1 > over empty? 0= and while
   dup stream>ch >r rot dup r> swap c! 1+ rot 1- rot repeat 2drop 0 swap c! ;

forth definitions
: dump-file ( a n a n -- )
  w/o create-file throw
  >r r@ write-file throw
  r> close-file drop
;

: cp ( "src" "dst" -- )
  bl parse r/o bin open-file throw { inf }
  bl parse w/o bin create-file throw { outf }
  begin
    here 80 inf read-file throw
    dup 0= if drop outf close-file throw inf close-file throw exit then
    here swap outf write-file throw
  again
;

: mv ( "src" "dst" -- ) bl parse bl parse rename-file throw ;
: rm ( "path" -- ) bl parse delete-file throw ;

: touch ( "path" -- )
  bl parse 2dup w/o open-file
  if drop w/o create-file throw then
  close-file throw
;

internals definitions

: cremit ( ch -- ) dup nl = if drop cr else emit then ;
: crtype ( a n -- ) for aft dup c@ cremit 1+ then next drop ;

forth definitions internals

: cat ( "path" -- )
  bl parse r/o bin open-file throw { fh }
  begin
    here 80 fh read-file throw
    dup 0= if drop fh close-file throw exit then
    here swap crtype
  again
;

DEFINED? read-dir [IF]
: ls ( "path" -- )
  bl parse open-dir throw { dh } begin
    dh read-dir dup 0= if
      2drop dh close-dir throw exit
    then type cr
  again
;
[THEN]

internals definitions
( Leave some room for growth of starting system. )
0 value saving-base
: park-heap ( -- a ) saving-base ;
: park-forth ( -- a ) saving-base cell+ ;
: 'cold ( -- a ) saving-base 2 cells + ;
: setup-saving-base
  here to saving-base  16 cells allot  0 'cold ! ;

' forth >body constant forth-wordlist

: save-name
  'heap @ park-heap !
  forth-wordlist @ park-forth !
  w/o create-file throw >r
  saving-base here over - r@ write-file throw
  r> close-file throw ;

: restore-name ( "name" -- )
  r/o open-file throw >r
  saving-base r@ file-size throw r@ read-file throw drop
  r> close-file throw
  park-heap @ 'heap !
  park-forth @ forth-wordlist !
  'cold @ dup if execute else drop then ;

defer remember-filename
: default-remember-filename   s" myforth" ;
' default-remember-filename is remember-filename

forth definitions also internals

: save ( "name" -- ) bl parse save-name ;
: restore ( "name" -- ) bl parse restore-name ;
: remember   remember-filename save-name ;
: startup: ( "name" ) ' 'cold ! remember ;
: revive   remember-filename restore-name ;
: reset   remember-filename delete-file throw ;

only forth definitions
( Including Files )

internals definitions

: ends/ ( a n -- f ) 1- + c@ [char] / = ;
: dirname ( a n -- )
   dup if
     2dup ends/ if 1- then
   then
   begin dup while
     2dup ends/ if exit then 1-
   repeat ;

: starts./ ( a n -- f )
   2 < if drop 0 exit then
   2 s" ./" str= ;

: starts../ ( a n -- f )
   3 < if drop 0 exit then
   3 s" ../" str= ;

0 value sourcefilename&
0 value sourcefilename#
: sourcefilename ( -- a n ) sourcefilename& sourcefilename# ;
: sourcefilename! ( a n -- ) to sourcefilename# to sourcefilename& ;
: sourcedirname ( -- a n ) sourcefilename dirname ;

: include-file ( fh -- )
   dup file-size throw
   dup allocate throw
   swap over >r
   rot read-file throw
   r@ swap evaluate
   r> free throw ;

: raw-included ( a n -- )
   r/o open-file throw
   dup >r include-file
   r> close-file throw ;

0 value included-files

: path-join { a a# b b# -- a n }
  a# b# + { r# } r# cell+ cell+ allocate throw { r }
  2 cells +to r
  b c@ [char] / = if 0 to a# then
  begin b b# starts./ while
    2 +to b -2 +to b#
    a# b# + to r#
  repeat
  begin b b# starts../ a# 0<> and while
    3 +to b -3 +to b#
    a a# dirname to a# to a
    a# b# + to r#
  repeat
  a r a# cmove b r a# + b# cmove
  r# r cell - !
  r r# ;
: include+ 2 cells - { a }
  included-files a ! a to included-files ;

forth definitions internals

: included ( a n -- )
   sourcefilename >r >r
   >r >r sourcedirname r> r> path-join 2dup sourcefilename!
   ['] raw-included catch
   dup if ." Error including: " sourcefilename type cr then
   sourcefilename& include+
   r> r> sourcefilename!
   throw ;

: include ( "name" -- ) bl parse included ;

: included? { a n -- f }
  sourcedirname a n path-join to n to a
  included-files begin dup while
    dup cell+ cell+ over cell+ @ a n str= if
      a 2 cells - free throw drop -1 exit
    then @
  repeat
  a 2 cells - free throw ;

: required ( a n -- ) 2dup included? if 2drop else included then ;
: needs ( "name" -- ) bl parse required ;

forth
( Block Files )
internals definitions
: clobber-line ( a -- a' ) dup 63 blank 63 + nl over c! 1+ ;
: clobber ( a -- ) 15 for clobber-line next drop ;
0 value block-dirty
create block-data 1024 allot
forth definitions internals

-1 value block-fid   variable scr   -1 value block-id
: open-blocks ( a n -- )
   block-fid 0< 0= if block-fid close-file throw -1 to block-fid then
   2dup r/w open-file if drop r/w create-file throw else nip nip then to block-fid ;
: use ( "name" -- ) bl parse open-blocks ;
defer default-use
internals definitions
: common-default-use s" blocks.fb" open-blocks ;
' common-default-use is default-use
: use?!   block-fid 0< if default-use then ;
: grow-blocks ( n -- ) 1024 * block-fid file-size throw max block-fid resize-file throw ;
forth definitions internals
: save-buffers
   block-dirty if
     block-id grow-blocks block-id 1024 * block-fid reposition-file throw
     block-data 1024 block-fid write-file throw
     block-fid flush-file throw
     0 to block-dirty
   then ;
: block ( n -- a ) use?! dup block-id = if drop block-data exit then
                   save-buffers dup grow-blocks
                   dup 1024 * block-fid reposition-file throw
                   block-data clobber
                   block-data 1024 block-fid read-file throw drop
                   to block-id block-data ;
: buffer ( n -- a ) use?! dup block-id = if drop block-data exit then
                    save-buffers to block-id block-data ;
: empty-buffers   -1 to block-id ;
: update   -1 to block-dirty ;
: flush   save-buffers empty-buffers ;

( Loading )
: load ( n -- ) block 1024 evaluate ;
: thru ( a b -- ) over - 1+ for aft dup >r load r> 1+ then next drop ;

( Utility )
: copy ( from to -- )
   swap block pad 1024 cmove pad swap block 1024 cmove update ;

( Editing )
: list ( n -- ) scr ! ." Block " scr @ . cr scr @ block
   15 for dup 63 type [char] | emit space 15 r@ - . cr 64 + next drop ;
internals definitions
: @line ( n -- ) 64 * scr @ block + ;
: e' ( n -- ) @line clobber-line drop update ;
forth definitions internals
vocabulary editor   also editor definitions
: l    scr @ list ;   : n    1 scr +! l ;  : p   -1 scr +! l ;
: wipe   15 for r@ e' next l ;   : e   e' l ;
: d ( n -- ) dup 1+ @line swap @line 15 @line over - cmove 15 e ;
: r ( n "line" -- ) 0 parse 64 min rot dup e @line swap cmove l ;
: a ( n "line" -- ) dup @line over 1+ @line 16 @line over - cmove> r ;
only forth definitions
( ANSI Codes )
vocabulary ansi   ansi definitions
: esc   27 emit ;   : bel   7 emit ;
: clear-to-eol   esc ." [0K" ;
: scroll-down   esc ." D" ;
: scroll-up   esc ." M" ;
: hide   esc ." [?25l" ;
: show   esc ." [?25h" ;
: terminal-save   esc ." [?1049h" ;
: terminal-restore   esc ." [?1049l" ;

forth definitions ansi
: fg ( n -- ) esc ." [38;5;" n. ." m" ;
: bg ( n -- ) esc ." [48;5;" n. ." m" ;
: normal   esc ." [0m" ;
: at-xy ( x y -- ) esc ." [" 1+ n. ." ;" 1+ n. ." H" ;
: page   esc ." [2J" esc ." [H" ;
: set-title ( a n -- ) esc ." ]0;" type bel ;
forth
( Lazy loaded visual editor. )

: visual r|

also DEFINED? termios [IF] termios [THEN]
also internals
also ansi
also forth
current @
vocabulary visual  visual definitions
vocabulary insides  insides definitions

256 constant max-path
create filename max-path allot 0 value filename#
0 value fileh

10 constant start-size
start-size allocate throw value text
start-size value capacity
0 value length
0 value caret

: up ( n -- n ) begin dup 0 > over text + c@ nl <> and while 1- repeat 1- 0 max ;
: nup ( n -- n ) 10 for up next ;
: down ( n -- n ) begin dup length < over text + c@ nl <> and while 1+ repeat 1+ length min ;
: ndown ( n -- n ) 10 for down next ;

: update
    caret nup dup 0<> if 1+ 1+ then { before }
    before ndown ndown { after }
    page
    text before + caret before - crtype
    caret length < text caret + c@ nl <> and if
      1 bg text caret + c@ emit normal
      text caret + 1+ after caret - 1- 0 max crtype
    else
      1 bg space normal
      text caret + after caret - crtype
    then normal
;

: insert ( ch -- )
  length capacity = if text capacity 1+ 2* >r r@ 1+ resize throw to text r> to capacity then
  text caret + dup 1+ length caret - cmove>
  text caret + c!
  1 +to caret
  1 +to length
  update
;

: handle-esc
    key
    dup [char] [ = if drop
       key
       dup [char] A = if drop caret up to caret update exit then
       dup [char] B = if drop caret down to caret update exit then
       dup [char] C = if drop caret 1+ length min to caret update exit then
       dup [char] D = if drop caret 1- 0 max to caret update exit then
       dup [char] 5 = if drop key drop caret 8 for up next to caret update exit then
       dup [char] 6 = if drop key drop caret 8 for down next to caret update exit then
       drop
       exit
    then
    drop
;

: delete
    length caret > if
      text caret + dup 1+ swap length caret - 1- 0 max cmove
      -1 +to length
      update
    then
;

: backspace
    caret 0 > if
        -1 +to caret
        delete
    then
;

: load ( a n -- )
     0 to caret
     dup to filename#
     filename swap cmove
     filename filename# r/o open-file 0= if
         to fileh
         fileh file-size throw to capacity
         text capacity 1+ resize throw to text
         capacity to length
         text length fileh read-file throw drop
         fileh close-file throw
     else
         drop
         0 to capacity
         0 to length
     then
;

: save
     filename filename# w/o create-file throw to fileh
     text length fileh write-file throw
     fileh close-file throw
;

: quit-edit
     page filename filename# type cr ." SAVE? "
     begin
         key 95 and
         dup [char] Y = if drop save 123 throw then
         dup [char] N = if drop 123 throw then
         drop
     again
;

: handle-key ( ch -- )
    dup 27 = if drop handle-esc exit then
    dup [char] D [char] @ - = if delete exit then
    dup [char] H [char] @ - = over 127 = or if drop backspace exit then
    dup [char] L [char] @ - = if drop update exit then
    dup [char] S [char] @ - = if drop save update exit then
    dup [char] X [char] @ - = if drop quit-edit then
    dup [char] Q [char] @ - = if drop quit-edit then
    dup 13 = if drop nl insert exit then
    dup bl >= if insert else drop then
;

: ground   depth 0<> throw ;
: step   *key handle-key ground ;

DEFINED? raw-mode 0= [IF]
    : raw-mode ;
    : normal-mode ;
[THEN]

: run
    raw-mode update
    begin
        ['] step catch
        dup 123 = if drop normal-mode page exit then
        if ." FAILURE!" then
    again
;

visual definitions insides

: edit ( <filename> ) bl parse load run ;

previous previous previous previous current ! visual
| evaluate ;
( Add a yielding task so pause yields )
internals definitions
: yield-step   raw-yield yield ;
' yield-step 100 100 task yield-task
yield-task start-task
forth definitions

( Set up Basic I/O )
internals definitions also serial
: esp32-bye   0 terminate ;
: serial-type ( a n -- ) Serial.write drop ;
: serial-key ( -- n )
   begin pause Serial.available until 0 >r rp@ 1 Serial.readBytes drop r> ;
: serial-key? ( -- n ) Serial.available ;
also forth definitions
: default-type serial-type ;
: default-key serial-key ;
: default-key? serial-key? ;
' default-type is type
' default-key is key
' default-key? is key?
' esp32-bye is bye
only forth definitions

also ledc also serial also SPIFFS

( Map Arduino / ESP32 things to shorter names. )
: pin ( n pin# -- ) swap digitalWrite ;
: adc ( n -- n ) analogRead ;
: duty ( n n -- ) 255 min 8191 255 */ ledcWrite ;
: freq ( n n -- ) 1000 * 13 ledcSetup drop ;
: tone ( n n -- ) 1000 * ledcWriteTone drop ;

( Basic Ardiuno Constants )
0 constant LOW
1 constant HIGH
1 constant INPUT
2 constant OUTPUT
2 constant LED

( Startup Setup )
-1 echo !
115200 Serial.begin
100 ms
-1 z" /spiffs" 10 SPIFFS.begin drop
led OUTPUT pinMode
high led pin

internals definitions also ESP
: esp32-stats
  getChipModel z>s type ."    "
  getCpuFreqMHz . ." MHz   "
  getChipCores .  ." cores   "
  getFlashChipSize . ." bytes flash" cr
  ."      System Heap: " getFreeHeap getHeapSize free. cr
  ."                   " getMaxAllocHeap . ." bytes max contiguous" cr ;
' esp32-stats internals boot-prompt !
only forth definitions

( Setup entry )
internals : ok   ." ESP32forth" raw-ok ; forth
( Lazy loaded assembler/disassembler framework )
: assembler r|

current @
also internals
also asm definitions

-1 1 rshift invert constant high-bit
: odd? ( n -- f ) 1 and ;
: >>1 ( n -- n ) 1 rshift ;
: enmask ( n m -- n )
  0 -rot cell 8 * 1- for
    rot >>1 -rot
    dup odd? if
      over odd? if rot high-bit or -rot then
      swap >>1 swap
    then
    >>1
  next
  2drop
;
: demask ( n m -- n )
  0 >r begin dup while
    dup 0< if over 0< if r> 2* 1+ >r else r> 2* >r then then
    2* swap 2* swap
  repeat 2drop r>
;

variable length   variable pattern   variable mask
: bit! ( n a -- ) dup @ 2* rot 1 and or swap ! ;

: >opmask& ( xt -- a ) >body ;
: >next ( xt -- xt ) >body cell+ @ ;
: >inop ( a -- a ) >body 2 cells + @ ;
: >printop ( a -- a ) >body 3 cells + @ ;

variable operands
: for-operands ( xt -- )
   >r operands @ begin dup while r> 2dup >r >r execute r> >next repeat rdrop drop ;

: reset-operand ( xt -- ) >opmask& 0 swap ! ;
: reset   0 length !  0 mask !  0 pattern !  ['] reset-operand for-operands ;
: advance-operand ( xt -- ) >opmask& 0 swap bit! ;
: advance   ['] advance-operand for-operands ;

: skip  1 length +!  0 mask bit!  0 pattern bit!  advance ;
: bit ( n -- ) 1 length +!  1 mask bit!  pattern bit!  advance ;
: bits ( val n ) 1- for dup r@ rshift bit next drop ;
: o   0 bit ;   : l   1 bit ;

( struct: pattern next inop printop )
: operand ( inop printop "name" )
   create 0 , operands @ , latestxt operands ! swap , ,
   does> skip 1 swap +! ;
: names ( n "names"*n --) 0 swap 1- for dup constant 1+ next drop ;

: coden, ( val n -- ) 8 / 1- for dup code1, 8 rshift next drop ;

( struct: length pattern mask [xt pattern]* 0 )
variable opcodes
: op-snap ( xt -- ) dup >opmask& @ if dup , >opmask& @ , else drop then ;
: >xt ( a -- xt ) 2 cells - ;
: >length ( xt -- a ) >body cell+ @ ;
: >pattern ( xt -- a ) >body 2 cells + @ ;
: >mask ( xt -- a ) >body 3 cells + @ ;
: >operands ( xt -- a ) >body 4 cells + ;
: op ( "name" )
   create opcodes @ , latestxt opcodes !
          length @ , pattern @ , mask @ ,
          ['] op-snap for-operands 0 , reset
   does> >xt >r
         r@ >pattern
         0 r@ >operands begin dup @ while >r 1+ r> 2 cells + repeat
         swap for aft
           2 cells - dup >r swap >r dup cell+ @ >r @ >inop execute r> enmask r> or r>
         then next
         drop
         r> >length coden,
;

: for-ops ( xt -- )
   >r opcodes @ begin dup while r> 2dup >r >r execute r> >body @ repeat rdrop drop ;

: m@ ( a -- n ) 0 swap cell 0 do dup ca@ i 8 * lshift swap >r or r> 1+ loop drop ;
: m. ( n n -- ) base @ hex >r >r <# r> 1- for # # next #> type r> base ! ;
: sextend ( n n -- n ) cell 8 * swap - dup >r lshift r> arshift ;

variable istep
variable address
: matchit ( a xt -- a )
  >r dup m@ r@ >mask and r@ >pattern = if
    r@ >operands begin dup @ while
      >r dup m@ r@ cell+ @ demask r@ @ >printop execute r> 2 cells +
    repeat drop
    r@ see.
    r@ >length 8 / istep !
  then rdrop ;
: disasm1 ( a -- a )
  dup address ! dup . ."  --  " 0 istep ! ['] matchit for-ops
  istep @ 0= if 1 istep ! ." UNKNOWN!!!" then
  9 emit 9 emit ." -- " dup m@ istep @ m.
  istep @ +
  cr
;
: disasm ( a n -- ) for aft disasm1 then next drop ;

previous previous
also forth definitions
: assembler asm ;
previous
assembler
current !

| evaluate ;
( Lazy loaded xtensa assembler )
: xtensa-assembler r|

current @
also assembler definitions
vocabulary xtensa xtensa definitions

16 names a0 a1 a2 a3 a4 a5 a6 a7 a8 a9 a10 a11 a12 a13 a14 a15
: nop ;
: reg. ( n -- ) base @ >r decimal ." a" . r> base ! ;
: register ( -- in print ) ['] nop ['] reg. ;
: numeric ( -- in print ) ['] nop ['] . ;

numeric operand im
: imm4   im im im im ;
: imm8   imm4 imm4 ;
: imm12   imm4 imm4 imm4 ;
: imm16   imm8 imm8 ;
: sr   imm8 ;

( Offsets for J )
: >ofs ( n -- n ) chere - 4 - ;
: ofs. ( n -- ) 18 sextend address @ + 4 + . ;
' >ofs ' ofs. operand ofs
: offset   18 for aft ofs then next ;

( Offsets for CALL* )
: >cofs ( n -- n ) chere - 2 rshift 1- ;
: cofs. ( n -- ) 18 sextend 1+ 2 lshift address @ 3 invert and + . ;
' >cofs ' cofs. operand cofs
: coffset   18 for aft cofs then next ;

( Frame size of ENTRY )
: >entry12 ( n -- n ) 3 rshift ;
: entry12. ( n -- ) 3 lshift . ;
' >entry12 ' entry12. operand entry12'
: entry12   12 for aft entry12' then next ;

: >sa ( n -- n ) 32 swap - ;
: sa. ( n -- ) 32 swap - . ;
' >sa ' sa. operand sa

numeric operand x   : xxxx   x x x x ;
numeric operand i   : iiii   i i i i ;
numeric operand w
numeric operand y
numeric operand b   : bbbb   b b b b ;

register operand r   : rrrr   r r r r ;
register operand s   : ssss   s s s s ;
register operand t   : tttt   t t t t ;

imm4     ssss     tttt     l o o o  OP L32I.N,
imm4     ssss     tttt     l o o l  OP S32I.N,
rrrr     ssss     tttt     l o l o  OP ADD.N,
rrrr     ssss     imm4     l o l l  OP ADDI.N,
iiii     ssss     l o i i  l l o o  OP BEQZ.N,
iiii     ssss     l l i i  l l o o  OP BNEZ.N,
iiii     ssss     o i i i  l l o o  OP MOVI.N,
o o o o  ssss     tttt     l l o l  OP MOV.N,
l l l l  ssss     o o l o  l l o l  OP BREAK.N,
l l l l  o o o o  o o o o  l l o l  OP RET.N,
l l l l  o o o o  o o o l  l l o l  OP RETW.N,
l l l l  o o o o  o o l l  l l o l  OP NOP.N,
l l l l  o o o o  o l l o  l l o l  OP ILL.N,

o o o o  o o o o  o o o o  o o o o  o o o o  o o o o  OP ILL,
o o o o  o o o o  o o o o  o o o o  l o o o  o o o o  OP RET,
o o o o  o o o o  o o o o  o o o o  l o o l  o o o o  OP RETW,
o o o o  o o o o  o o l o  o o o o  o o o o  o o o o  OP ISYNC,
o o o o  o o o o  o o l o  o o o o  o o o l  o o o o  OP RSYNC,
o o o o  o o o o  o o l o  o o o o  o o l o  o o o o  OP ESYNC,
o o o o  o o o o  o o l o  o o o o  o o l l  o o o o  OP DSYNC,
o o o o  o o o o  o o l o  o o o o  l o o o  o o o o  OP EXCW,
o o o o  o o o o  o o l o  o o o o  l l o o  o o o o  OP MEMW,
o o o o  o o o o  o o l o  o o o o  l l o l  o o o o  OP EXTW,
o o o o  o o o o  o o l o  o o o o  l l l l  o o o o  OP NOP,
o o o o  o o o o  o o l l  o o o o  o o o o  o o o o  OP RFE,
o o o o  o o o o  o o l l  o o o o  o o l o  o o o o  OP RFME,
o o o o  o o o o  o o l l  o o o l  o o o o  o o o o  OP RFUE,
o o o o  o o o o  o o l l  o o l o  o o o o  o o o o  OP RFDE,
o o o o  o o o o  o o l l  o l o o  o o o o  o o o o  OP RFWO,
o o o o  o o o o  o o l l  o l o l  o o o o  o o o o  OP RFWU,
o o o o  o o o o  o l o l  o o o o  o o o o  o o o o  OP SYSCALL,
o o o o  o o o o  o l o l  o o o l  o o o o  o o o o  OP SIMCALL,
l l l l  o o o l  l l l o  o o o s  o o o l  o o o o  OP RFDD,
l l l l  o o o l  l l l o  o o o o  o o o o  o o o o  OP RFDO,

o l l o  o o o o  rrrr  o o o o  tttt  o o o o  OP NEG,
o l l o  o o o o  rrrr  o o o l  tttt  o o o o  OP ABS,
o l l o  o o o l  sr             tttt  o o o o  OP XSR,

: ALU   4 bits  o o o o  rrrr  ssss  tttt  o o o o  OP ;
              $1 ALU AND,   $2 ALU OR,     $3 ALU XOR,
( $6 ABS/NEG )
$8 ALU ADD,   $9 ALU ADDX2, $a ALU ADDX4,  $b ALU ADDX8,
$c ALU SUB,   $d ALU SUBX2, $e ALU SUBX4,  $f ALU SUBX8,

: ANYALL   o o o o  o o o o  l o 2 bits  ssss  tttt  o o o o  OP ;
$0 ANYALL ANY4,  $1 ANYALL ALL4,  $2 ANYALL ANY8,  $3 ANYALL ALL8,

: ALU2   4 bits  o o l o  rrrr  ssss  tttt  o o o o  OP ;
$0 ALU2 ANDB,  $1 ALU2 ANDBC,  $2 ALU2 ORB,    $3 ALU2 ORBC,
$4 ALU2 XORB,
$8 ALU2 MULL,                  $a ALU2 MULUH,  $b ALU2 MULSH,
$c ALU2 QUOU,  $d ALU2 QUOS,   $e ALU2 REMU,   $f ALU2 REMS,

: BRANCH1   imm8  4 bits  ssss  tttt  o l l l  OP ;
$0 BRANCH1 BNONE,  $1 BRANCH1 BEQ,  $2 BRANCH1 BLT,  $3 BRANCH1 BLTU,
$4 BRANCH1 BALL,   $5 BRANCH1 BBC,
imm8  o l l b  ssss  bbbb  o l l l  OP BBCI,
$8 BRANCH1 BANY,   $9 BRANCH1 BNE,  $a BRANCH1 BGE,  $b BRANCH1 BGEU,
$c BRANCH1 BNALL,  $d BRANCH1 BBS,
imm8  l l l b  ssss  bbbb  o l l l  OP BBSI,

: BRANCH2   imm12  ssss  4 bits  o l l o  OP ;
: BRANCH2a   imm8  rrrr     ssss  4 bits  o l l o  OP ;
: BRANCH2e   entry12  ssss  4 bits  o l l o  OP ;
( $0 J, )  $1 BRANCH2 BEQZ,  $2 BRANCH2a BEQI,  $3 BRANCH2e ENTRY,
( $4 J, )  $5 BRANCH2 BNEZ,  $6 BRANCH2a BNEI,  ( BRANCH2b's )
( $8 J, )  $9 BRANCH2 BLTZ,  $a BRANCH2a BLTI,  $b BRANCH2a BLTUI,
( $c J, )  $d BRANCH2 BGEZ,  $e BRANCH2a BGEI,  $f BRANCH2a BGEUI,
offset  o o  o l l o  OP J,
: BRANCH2b   imm8  4 bits  ssss  o l l l  o l l o  OP ;
$0 BRANCH2b BF,    $1 BRANCH2b BT,
$8 BRANCH2b LOOP,  $9 BRANCH2b LOOPNEZ,  $a BRANCH2b LOOPGTZ,

: CALLOP   coffset  2 bits  o l o l  OP ;
0 CALLOP CALL0,  1 CALLOP CALL4,  2 CALLOP CALL8,  3 CALLOP CALL12,

: CALLXOP   o o o o  o o o o  o o o o  ssss  l l 2 bits  o o o o  OP ;
0 CALLXOP CALLX0,  1 CALLXOP CALLX4,  2 CALLXOP CALLX8,  3 CALLXOP CALLX12,

o o o o  o o o o  o l o o  ssss  tttt  o o o o  OP BREAK,
o o l l  o o l l  rrrr     ssss  tttt  o o o o  OP CLAMPS,

: CACHING1 imm8  o l l l  ssss  4 bits  o o l o  OP ;
$0 CACHING1 DPFR,  $1 CACHING1 DPFW,   $2 CACHING1 DPFRO,  $3 CACHING1 DPFWO,
$4 CACHING1 DHWB,  $5 CACHING1 DHWBI,  $6 CACHING1 DHI,    $7 CACHING1 DII,
( $8 CACHING2 )    ( ?? )              ( ?? )              ( ?? )
$c CACHING1 IPF,   ( $d CACHING2 )     $e CACHING1 IHI,    $f CACHING1 III,

: CACHING2 imm4  4 bits  o l l l  ssss  4 bits  o o l o  OP ;
$0 $8 CACHING2 DPFL,  $2 $8 CACHING2 DHU,    $3 $8 CACHING2 DIU,
$4 $8 CACHING2 DIWB,  $5 $8 CACHING2 DIWBI,
$0 $d CACHING2 IPFL,  $2 $d CACHING2 IHU,    $3 $d CACHING2 IIU,

iiii  iiii     l o l o  iiii  tttt     o o l o  OP MOVI,

: LDSTORE  imm8  4 bits  ssss  tttt  o o l o  OP ;
$0 LDSTORE L8UI,  $1 LDSTORE L16UI,  $2 LDSTORE L32I,    ( $3 CACHING )
$4 LDSTORE S8I,   $5 LDSTORE S16I,   $6 LDSTORE S32I,
                  $9 LDSTORE L16SI,  ( $a MOVI )         $b LDSTORE L32AI,
$c LDSTORE ADDI,  $d LDSTORE ADDMI,  $e LDSTORE S32C1I,  $f LDSTORE S32RI,

o l o o  l o o l  rrrr  ssss  tttt  o o o o  OP S32E,

x x x x  o l o sa  rrrr  sa sa sa sa  tttt  o o o o  OP EXTUI,

imm16  tttt  o o o l  OP L32R,
l o o l  o o o o  o o w w  ssss  o o o o  o l o o  OP LDDEC,
l o o o  o o o o  o o w w  ssss  o o o o  o l o o  OP LDINC,
imm8  o o o o  ssss  tttt  o o l l  OP LSI,
imm8  l o o o  ssss  tttt  o o l l  OP LSIU,

o l o l  o o o o  l l o o  ssss  o o o o  o o o o  OP IDTLB,
o l o l  o o o o  o l o o  ssss  o o o o  o o o o  OP IITLB,
o o o o  o o o o  o o o o  ssss  l o l o  o o o o  OP JX,
l l l l  o o o l  l o o o  ssss  tttt     o o o o  OP LDCT,
l l l l  o o o l  o o o o  ssss  tttt     o o o o  OP LICT,
l l l l  o o o l  o o l o  ssss  tttt     o o o o  OP LICW,
o o o o  l o o l  rrrr     ssss  tttt     o o o o  OP L32E,
o o o o  l o o o  rrrr     ssss  tttt     o o o o  OP LSX,
o o o l  l o o o  rrrr     ssss  tttt     o o o o  OP LSXU,
o o l o  o o o o  rrrr     ssss  tttt     o o o o  OP MOV,

: CONDOP   4 bits  o o l l  rrrr  ssss  tttt  o o o o  OP ;
$4 CONDOP MIN,     $5 CONDOP MAX,     $6 CONDOP MINU,    $7 CONDOP MAXU,
$8 CONDOP MOVEQZ,  $9 CONDOP MOVNEZ,  $a CONDOP MOVLTZ,  $b CONDOP MOVGEZ,
$c CONDOP MOVF,

: ALU.S   4 bits  l o l o  rrrr  ssss  tttt  o o o o  OP ;
$0 ALU.S ADD.S,    $1 ALU.S SUB.S,    $2 ALU.S MUL.S,
$4 ALU.S MADD.S,   $5 ALU.S MSUB.S,
$8 ALU.S ROUND.S,  $9 ALU.S TRUNC.S,  $a ALU.S FLOOR.S,  $b ALU.S CEIL.S,
$c ALU.S FLOAT.S,  $d ALU.S UFLOAT.S, $e ALU.S UTRUNC.S,
: ALU2.S   l l l l  l o l o  rrrr  ssss  4 bits  o o o o  OP ;
$0 ALU2.S MOV.S,   $1 ALU2.S ABS.S,
$4 ALU2.S RFR,     $5 ALU2.S WFR,     $6 ALU2.S NEG.S,

: CMPSOP   4 bits  l o l l  rrrr  ssss  tttt  o o o o  OP ;
                     $1 CMPSOP UN.S,      $2 CMPSOP OEQ.S,    $3 CMPSOP UEQ.S,
$4 CMPSOP OLT.S,     $5 CMPSOP ULT.S,     $6 CMPSOP OLE.S,    $7 CMPSOP ULE.S,
$8 CMPSOP MOVEQZ.S,  $9 CMPSOP MOVNEZ.S,  $a CMPSOP MOVLTZ.S, $b CMPSOP MOVGEZ.S,
$c CMPSOP MOVF.S,    $d CMPSOP MOVT.S,

o o o o  o o o o  o o o l  ssss  tttt     o o o o  OP MOVSP,

l l o l  o o l l  rrrr  ssss  tttt  o o o o  OP MOVT,
: MUL.AA o l l l  o l 2 bits  o o o o  ssss  tttt  o l o o  OP ;
0 MUL.AA MUL.AA.LL,   1 MUL.AA MUL.AA.HL,  2 MUL.AA MUL.AA.LH,  3 MUL.AA MUL.AA.HH,
: MUL.AD o l l l  o l 2 bits  o o o o  ssss  o y o o  o l o o  OP ;
0 MUL.AD MUL.AD.LL,   1 MUL.AD MUL.AD.HL,  2 MUL.AD MUL.AD.LH,  3 MUL.AD MUL.AD.HH,
: MUL.DA o l l o  o l 2 bits  o x o o  o o o o  tttt  o l o o  OP ;
0 MUL.DA MUL.DA.LL,  1 MUL.DA MUL.DA.HL,  2 MUL.DA MUL.DA.LH,  3 MUL.DA MUL.DA.HH,
: MUL.DD o o l o  o l 2 bits  o x o o  o o o o  o y o o  o l o o  OP ;
0 MUL.DD MUL.DD.LL,  1 MUL.DD MUL.DD.HL,  2 MUL.DD MUL.DD.LH,  3 MUL.DD MUL.DD.HH,
l l o l  o o o l  rrrr  ssss  tttt  o o o o  OP MUL16S,
l l o o  o o o l  rrrr  ssss  tttt  o o o o  OP MUL16U,
: MULA.AA o l l l  l o 2 bits  ssss  tttt o l o o  OP ;
0 MULA.AA MULA.AA.LL,  1 MULA.AA MULA.AA.HL,  2 MULA.AA MULA.AA.LH,  3 MULA.AA MULA.AA.HH,
: MULA.AD o o l l  l o 2 bits ssss  tttt o l o o  OP ;
0 MULA.AD MULA.AD.LL,  1 MULA.AD MULA.AD.HL,  2 MULA.AD MULA.AD.LH,  3 MULA.AD MULA.AD.HH,
: MULA.DA o l l o  l o 2 bits  o x o o  o o o o  tttt o l o o  OP ;
0 MULA.DA MULA.DA.LL,  1 MULA.DA MULA.DA.HL,  2 MULA.DA MULA.DA.LH,  3 MULA.DA MULA.DA.HH,
: MULA.DA.LDDEC o l o l  l o 2 bits  o x o o  o o o o  tttt o l o o  OP ;
0 MULA.DA.LDDEC MULA.DA.LL.LDDEC,  1 MULA.DA.LDDEC MULA.DA.HL.LDDEC,
2 MULA.DA.LDDEC MULA.DA.LH.LDDEC,  3 MULA.DA.LDDEC MULA.DA.HH.LDDEC,
: MULA.DA.LDINC o l o o  l o 2 bits  o x w o  o o o o  tttt o l o o  OP ;
0 MULA.DA.LDINC MULA.DA.LL.LDINC,  1 MULA.DA.LDINC MULA.DA.HL.LDINC,
2 MULA.DA.LDINC MULA.DA.LH.LDINC,  3 MULA.DA.LDINC MULA.DA.HH.LDINC,
: MULA.DD o o l o  l o 2 bits  o x o o  o o o o  o y o o  o l o o  OP ;
0 MULA.DD MULA.DD.LL,  1 MULA.DD MULA.DD.HL,  2 MULA.DD MULA.DD.LH,  3 MULA.DD MULA.DD.HH,
: MULA.DD.LDDEC o o o l  l o 2 bits  o x w w  o o o o  tttt o l o o  OP ;
0 MULA.DD.LDDEC MULA.DD.LL.LDDEC,  1 MULA.DD.LDDEC MULA.DD.HL.LDDEC,
2 MULA.DD.LDDEC MULA.DD.LH.LDDEC,  3 MULA.DD.LDDEC MULA.DD.HH.LDDEC,
: MULA.DD.LDINC o o o o  l o 2 bits  o x w w  o o o o  tttt o l o o  OP ;
0 MULA.DD.LDINC MULA.DD.LL.LDINC,  1 MULA.DD.LDINC MULA.DD.HL.LDINC,
2 MULA.DD.LDINC MULA.DD.LH.LDINC,  3 MULA.DD.LDINC MULA.DD.HH.LDINC,
: MULS.AA o l l l  l o 2 bits  o o o o  ssss  tttt  o l o o  OP ;
0 MULS.AA MULA.AA.LL,  1 MULS.AA MULA.AA.HL,  2 MULS.AA MULA.AA.LH,  3 MULS.AA MULA.AA.HH,
: MULS.AD o o l l  l o 2 bits  o o o o  ssss  o y o o  o l o o  OP ;
0 MULS.AD MULA.AD.LL,  1 MULS.AD MULA.AD.HL,  2 MULS.AD MULA.AD.LH,  3 MULS.AD MULA.AD.HH,
: MULS.DA o l l o  l o 2 bits  o x o o  o o o o  tttt  o l o o  OP ;
0 MULS.DA MULA.DA.LL,  1 MULS.DA MULA.DA.HL,  2 MULS.DA MULA.DA.LH,  3 MULS.DA MULA.DA.HH,
: MULS.DD o o l o  l o 2 bits  o x o o  o o o o  o y o o  o l o o  OP ;
0 MULS.DD MULA.DD.LL,  1 MULS.DD MULA.DD.HL,  2 MULS.DD MULA.DD.LH,  3 MULS.DD MULA.DD.HH,

o l o o  o o o o  l l l o  ssss  tttt  o o o o  OP NSA,
o l o o  o o o o  l l l l  ssss  tttt  o o o o  OP NSAU,

o l o l  o o o o  l l o l  ssss  tttt  o o o o  OP PDTLB,
o l o l  o o o o  o l o l  ssss  tttt  o o o o  OP PITLB,
o l o l  o o o o  l o l l  ssss  tttt  o o o o  OP RDTLB0,
o l o l  o o o o  l l l l  ssss  tttt  o o o o  OP RDTLB1,
o l o o  o o o o  o l l o  ssss  tttt  o o o o  OP RER,
o l o l  o o o o  o o l l  ssss  tttt  o o o o  OP RITLB0,
o l o l  o o o o  o l l l  ssss  tttt  o o o o  OP RITLB1,
o l o o  o o o o  l o o o  o o o o  imm4  o o o o  OP ROTW,
o o o o  o o o o  o o l l  imm4  o o o l  o o o o  OP RFI,
o o o o  o o o o  o l l o  imm4  tttt  o o o o  OP RSIL,
o o o o  o o l l  sr  tttt  o o o o  OP RSR,
l l l o  o o l l  rrrr  ssss  tttt  o o o o  OP RUR,
l l l l  o o o l  l o o l  ssss  tttt  o o o o  OP SDCT,
o o l o  o o l l  rrrr  ssss  tttt  o o o o  OP SEXT,
l l l l  o o o l  o o o l  ssss  tttt  o o o o  OP SICT,
l l l l  o o o l  o o l l  ssss  tttt  o o o o  OP SICW,
l o l o  o o o l  rrrr  ssss  o o o o  o o o o  OP SLL,
o o o sa  o o o l  rrrr  ssss  sa sa sa sa  o o o o  OP SLLI,
l o l l  o o o l  rrrr  o o o o  tttt  o o o o  OP SRA,
o o l x  o o o l  rrrr  xxxx  tttt  o o o o  OP SRAI,
l o o o  o o o l  rrrr  ssss  tttt  o o o o  OP SRC,
l o o l  o o o l  rrrr  o o o o  tttt  o o o o  OP SRL,
o l o o  o o o l  rrrr  xxxx  tttt  o o o o  OP SRLI,
o l o o  o o o o  o o l l  ssss  o o o o  o o o o  OP SSA8B,
o l o o  o o o o  o o l o  ssss  o o o o  o o o o  OP SSA8L,
o l o o  o o o o  o l o o  xxxx  o o o x  o o o o  OP SSAI,
imm8  o l o o  ssss  tttt  o o l l  OP SSI,
imm8  l l o o  ssss  tttt  o o l l  OP SSIU,
o l o o  o o o o  o o o l  ssss  o o o o  o o o o  OP SSL,
o l o o  o o o o  o o o o  ssss  o o o o  o o o o  OP SSR,
o l o o  l o o o  rrrr  ssss  tttt  o o o o  OP SSX,
o l o l  l o o o  rrrr  ssss  tttt  o o o o  OP SSXU,
( TODO: UMUL.AA.* )
o o o o  o o o o  o l l l  imm4  o o o o  o o o o  OP WAITI,
o l o l  o o o o  l l l o  ssss  tttt     o o o o  OP WDTLB,
o l o o  o o o o  o l l l  ssss  tttt     o o o o  OP WER,
o l o l  o o o o  o l l o  ssss  tttt     o o o o  OP WITLB,
o o o l  o o l l  sr             tttt     o o o o  OP WSR,
l l l l  o o l l  sr             tttt     o o o o  OP WUR,

also forth definitions
: xtensa-assembler xtensa ;
previous previous
xtensa-assembler
current !

| evaluate ;

[THEN]
( Lazy loaded RISC-V assembler )
: riscv-assembler r|

current @
also assembler definitions
vocabulary riscv riscv definitions

32 names zero x1 x2 x3 x4 x5 x6 x7 x8 x9 x10 x11 x12 x13 x14 x15 x16 x17 x18 x19 x20 x21 x22 x23 x24 x25 x26 x27 x28 x29 x30 x31
: nop ;
: reg. ( n -- ) dup if base @ >r decimal ." x" . r> base ! else drop ." zero " then ;
: register ( -- in print ) ['] nop ['] reg. ;

: reg>reg' ( n -- n ) 8 - 7 and ;
: reg'. ( n -- ) 8 + reg. ;
: register' ( -- in print ) ['] reg>reg' ['] reg'. ;

: numeric ( -- in print ) ['] nop ['] . ;

numeric operand i   : iiii   i i i i ;

( Offsets for JAL )
: >ofs ( n -- n ) chere - dup 20 rshift 1 and 31 12 - lshift
                          over 1 rshift $3ff and 21 12 - lshift or
                          over 11 rshift 1 and 20 12 - lshift or
                          swap 12 rshift $ff and or ;
: ofs. ( n -- ) dup 31 12 - rshift 1 and 20 lshift
                over 21 12 - rshift $3ff and 1 lshift or
                over 20 12 - rshift 1 and 11 lshift or
                swap $ff and 12 lshift or 21 sextend address @ + . ;
' >ofs ' ofs. operand ofs
: offset   20 for aft ofs then next ;

register  operand rd#    : rd    rd# rd# rd# rd# rd# ;
register' operand rd#'   : rd'   rd#' rd#' rd#' ;
register  operand rs1#   : rs1   rs1# rs1# rs1# rs1# rs1# ;
register' operand rs1#'  : rs1'  rs1#' rs1#' rs1#' ;
register  operand rs2#   : rs2   rs2# rs2# rs2# rs2# rs2# ;
register' operand rs2#'  : rs2'  rs2#' rs2#' rs2#' ;

: R-TYPE { fn7 fn3 opc }
  fn7 7 bits  rs2  rs1  fn3 3 bits  rd      opc 7 bits OP ;
: I-TYPE { fn3 opc }
  iiii iiii iiii   rs1  fn3 3 bits  rd      opc 7 bits OP ;
: S-TYPE { fn3 opc }
  iiii i i i  rs2  rs1  fn3 3 bits  i iiii  opc 7 bits OP ;
: B-TYPE { fn3 opc }
  iiii i i i  rs2  rs1  fn3 3 bits  i iiii  opc 7 bits OP ;
: U-TYPE { opc }
  iiii iiii iiii iiii iiii          rd      opc 7 bits OP ;
: J-TYPE { opc }
  offset                            rd      opc 7 bits OP ;

$37 U-TYPE LUI,
$17 U-TYPE AUIPC,
$6F J-TYPE JAL,
$0 $67 I-TYPE JALR,

$0 $63 B-TYPE BEQ,   $1 $63 B-TYPE BNE,
$4 $63 B-TYPE BLT,   $5 $63 B-TYPE BGE,
$6 $63 B-TYPE BLTU,  $7 $63 B-TYPE BGEU,

$0 $03 I-TYPE LB,   $1 $03 I-TYPE LH,   $2 $03 I-TYPE LW,
$4 $03 I-TYPE LBU,  $5 $03 I-TYPE LHU,

$0 $23 S-TYPE SB,  $1 $23 S-TYPE SH,  $2 $23 S-TYPE SW,

$0 $13 I-TYPE ADDI,  $2 $13 I-TYPE SLTI,  $3 $13 I-TYPE SLTIU,
$4 $13 I-TYPE XORI,  $6 $13 I-TYPE ORI,   $7 $13 I-TYPE ANDI,

$00 $1 $13 R-TYPE SLLI,
$00 $5 $13 R-TYPE SRLI,  $20 $5 $13 R-TYPE SRAI,

$00 $0 $33 R-TYPE ADD,   $20 $0 $33 R-TYPE SUB,

$00 $1 $33 R-TYPE SLL,  $00 $2 $33 R-TYPE SLT,
$00 $3 $33 R-TYPE SLTU, $00 $4 $33 R-TYPE XOR,

$00 $5 $33 R-TYPE SRL,  $20 $5 $33 R-TYPE SRA,

$00 $6 $33 R-TYPE OR,   $20 $7 $33 R-TYPE AND,

( TODO FENCE, FENCE.I )

o o o o o o o o o o o o   o o o o o  o o o  o o o o o  o o o l l l l OP ECALL,
o o o o o o o o o o o l   o o o o o  o o o  o o o o o  o o o l l l l OP EBREAK,

( TODO CSR* )

o o o  o o o o o o o o  o o o  o o  OP C.ILL,
o o o  i i i i i i i i  rd'    o o  OP C.ADDI4SP,
o o l  i i i  rs1' i i  rd'    o o  OP C.FLD,
o l o  i i i  rs1' i i  rd'    o o  OP C.LW,
o l l  i i i  rs1' i i  rd'    o o  OP C.FLW,
( 4 reserved )
l o l  i i i  rs1' i i  rd'    o o  OP C.FSD,
l l o  i i i  rs1' i i  rd'    o o  OP C.SW,
l l l  i i i  rs1' i i  rd'    o o  OP C.FSW,

o o o  o  o o o o o  o o o o o   o l  OP C.NOP,
o o o  i  rs1        i i i i i   o l  OP C.ADDI,
o o l  i  i i i i i  i i i i i   o l  OP C.JAL,
o l o  i  rd         i i i i i   o l  OP C.LI,
o l l  i  rd         i i i i i   o l  OP C.LUI,
l o o  i  o o  rs1'  i i i i i   o l  OP C.SRLI,
l o o  i  o l  rs1'  i i i i i   o l  OP C.SRAI,
l o o  i  l o  rs1'  i i i i i   o l  OP C.ANDI,
l o o  o  l l  rs1'  o o  rs2'   o l  OP C.SUB,
l o o  o  l l  rs1'  o l  rs2'   o l  OP C.XOR,
l o o  o  l l  rs1'  l o  rs2'   o l  OP C.OR,
l o o  o  l l  rs1'  l l  rs2'   o l  OP C.AND,
l o o  l  l l  rs1'  o o  rs2'   o l  OP C.SUBW,
l o o  l  l l  rs1'  o l  rs2'   o l  OP C.ADDW,
l o l  i  i i i i i  i i i i i   o l  OP C.J,
l l o  i  i i  rs1'  i i i i i   o l  OP BEQZ,
l l l  i  i i  rs1'  i i i i i   o l  OP BNEZ,

o o o  i  rs1        i i i i i   l o  OP C.SLLI,
o o l  i  rd         i i i i i   l o  OP C.FLDSP,
o l o  i  rd         i i i i i   l o  OP C.LWSP,
o l l  i  rd         i i i i i   l o  OP C.FLWSP,
l o o  o  rs1        o o o o o   l o  OP C.JR,
l o o  o  rd         rs2         l o  OP C.MV,
l o o  l  o o o o o  o o o o o   l o  OP C.EBREAK,
l o o  l  rs1        o o o o o   l o  OP C.JALR,
l o o  l  rd         rs2         l o  OP C.ADD,
l o l  i  i i i i i  rs2         l o  OP C.FSDSP,
l l o  i  i i i i i  rs2         l o  OP C.SWSP,
l l l  i  i i i i i  rs2         l o  OP C.FSWSP,

also forth definitions
: riscv-assembler riscv ;
previous previous
riscv-assembler
current !

| evaluate ;

[THEN]
( Lazy loaded HTTP Daemon )
: httpd r|

vocabulary httpd   httpd definitions
also sockets
also internals

1 constant max-connections
2048 constant chunk-size
create chunk chunk-size allot
0 value chunk-filled
256 constant body-chunk-size
create body-chunk body-chunk-size allot
0 value body-1st-read
0 value body-read

-1 value sockfd   -1 value clientfd
sockaddr httpd-port   sockaddr client   variable client-len

: client-type ( a n -- ) clientfd write-file throw ;
: client-read ( -- n ) 0 >r rp@ 1 clientfd read-file throw 1 <> throw ;
: client-emit ( ch -- ) >r rp@ 1 client-type rdrop ;
: client-cr   13 client-emit nl client-emit ;

: server ( port -- )
  httpd-port ->port!  ." Listening on port " httpd-port ->port@ . cr
  AF_INET SOCK_STREAM 0 socket to sockfd
(  sockfd SOL_SOCKET SO_REUSEADDR 1 >r rp@ 4 setsockopt rdrop throw )
  sockfd non-block throw
  sockfd httpd-port sizeof(sockaddr_in) bind throw
  sockfd max-connections listen throw
;

: upper ( ch -- ch )
  dup [char] a >= over [char] z <= and if 95 and then ;
: strcase= ( a n a n -- f )
  >r swap r@ <> if rdrop 2drop 0 exit then r>
  for aft
    2dup c@ upper swap c@ upper <> if 2drop 0 exit then
    1+ swap 1+ swap
  then next
  2drop -1
;

variable goal   variable goal#
: end< ( n -- f ) chunk-filled < ;
: in@<> ( n ch -- f ) >r chunk + c@ r> <> ;
: skipto ( n ch -- n )
   >r begin dup r@ in@<> over end< and while 1+ repeat rdrop ;
: skipover ( n ch -- n ) skipto 1+ ;
: eat ( n ch -- n a n ) >r dup r> skipover swap 2dup - 1- >r chunk + r> ;
: crnl= ( n -- f ) dup chunk + c@ 13 = swap 1+ chunk + c@ nl = and ;
: header ( a n -- a n )
  goal# ! goal ! 0 nl skipover
  begin dup end< while
    dup crnl= if drop chunk 0 exit then
    [char] : eat goal @ goal# @ strcase= if 1+ 13 eat rot drop exit then
    nl skipover
  repeat drop chunk 0
;
: content-length ( -- n )
  s" Content-Length" header s>number? 0= if 0 then ;
: body ( -- a n ) ( reads a part of body )
  body-1st-read if
    body-read content-length >= if
      0 0 exit
    then
    body-chunk body-chunk-size clientfd read-file throw dup +to body-read
    body-chunk swap exit
  then
  -1 to body-1st-read
  0 to body-read
  0 nl skipover
  begin dup end< while
    dup crnl= if
      2 + chunk-filled over - swap chunk + swap
      dup +to body-read exit
    then
    nl skipover
  repeat drop chunk 0
;
: completed? ( -- f )
  0 begin dup end< while
    dup crnl= if drop -1 exit then
    nl skipover
  repeat drop 0
;
: read-headers
  0 to body-1st-read
  0 to chunk-filled
  begin completed? 0= while
    chunk chunk-filled + chunk-size chunk-filled -
      clientfd read-file throw +to chunk-filled
  repeat
;

: handleClient
  clientfd close-file drop
  -1 to clientfd
  sockfd client client-len sockaccept
  dup 0< if drop 0 exit then
  to clientfd
  chunk chunk-size erase
  read-headers
  -1
;

: hasHeader ( a n -- f ) 2drop header 0 0 strcase= 0= ;
: method ( -- a n ) 0 bl eat rot drop ;
: path ( -- a n ) 0 bl skipover bl eat rot drop ;
: send ( a n -- ) client-type ;

: response ( mime$ result$ status -- )
  s" HTTP/1.0 " client-type <# #s #> client-type
  bl client-emit client-type client-cr
  s" Content-type: " client-type client-type client-cr
  client-cr ;
: ok-response ( mime$ -- ) s" OK" 200 response ;
: bad-response ( -- ) s" text/plain" s" Bad Request" 400 response ;
: notfound-response ( -- ) s" text/plain" s" Not Found" 404 response ;

only forth definitions
httpd
| evaluate ;
( Lazy loaded Server Terminal )

defer web-interface
:noname r~
httpd
also streams also httpd
vocabulary web-interface   also web-interface definitions

r|
<!html>
<head>
<title>esp32forth</title>
<style>
body {
  padding: 5px;
  background-color: #111;
  color: #2cf;
  overflow: hidden;
}
#prompt {
  width: 100%;
  padding: 5px;
  font-family: monospace;
  background-color: #ff8;
}
#output {
  width: 100%;
  height: 80%;
  resize: none;
  overflow-y: scroll;
  word-break: break-all;
}
</style>
<link rel="icon" href="data:,">
</head>
<body>
<h2>ESP32forth v7</h2>
Upload File: <input id="filepick" type="file" name="files[]"></input><br/>
<button onclick="ask('hex\n')">hex</button>
<button onclick="ask('decimal\n')">decimal</button>
<button onclick="ask('words\n')">words</button>
<button onclick="ask('low led pin\n')">LED OFF</button>
<button onclick="ask('high led pin\n')">LED ON</button>
<br/>
<textarea id="output" readonly></textarea>
<input id="prompt" type="prompt"></input><br/>
<script>
var prompt = document.getElementById('prompt');
var filepick = document.getElementById('filepick');
var output = document.getElementById('output');
function httpPost(url, data, callback) {
  var r = new XMLHttpRequest();
  r.onreadystatechange = function() {
    if (this.readyState == XMLHttpRequest.DONE) {
      if (this.status === 200) {
        callback(this.responseText);
      } else {
        callback(null);
      }
    }
  };
  r.open('POST', url);
  r.send(data);
}
setInterval(function() { ask(''); }, 300);
function ask(cmd, callback) {
  httpPost('/input', cmd, function(data) {
    if (data !== null) { output.value += data; }
    output.scrollTop = output.scrollHeight;  // Scroll to the bottom
    if (callback !== undefined) { callback(); }
  });
}
prompt.onkeyup = function(event) {
  if (event.keyCode === 13) {
    event.preventDefault();
    ask(prompt.value + '\n');
    prompt.value = '';
  }
};
filepick.onchange = function(event) {
  if (event.target.files.length > 0) {
    var reader = new FileReader();
    reader.onload = function(e) {
      var parts = e.target.result.replace(/[\r]/g, '').split('\n');
      function upload() {
        if (parts.length === 0) { filepick.value = ''; return; }
        ask(parts.shift(), upload);
      }
      upload();
    }
    reader.readAsText(event.target.files[0]);
  }
};
window.onload = function() {
  ask('\n');
  prompt.focus();
};
</script>
| constant index-html# constant index-html

variable webserver
2000 constant out-size
200 stream input-stream
out-size stream output-stream
create out-string out-size 1+ allot align

: handle-index
   s" text/html" ok-response
   index-html index-html# send
;

: handle-input
   begin body dup >r input-stream >stream pause r> 0= until
   out-string out-size output-stream stream>
   s" text/plain" ok-response
   out-string z>s send
;

: serve-type ( a n -- ) output-stream >stream ;
: serve-key ( -- n ) input-stream stream>ch ;

: handle1
  handleClient if
    s" /" path str= if handle-index exit then
    s" /input" path str= if handle-input exit then
    notfound-response
  then
;

: do-serve    begin handle1 pause again ;
' do-serve 1000 1000 task webserver-task

: server ( port -- )
   server
   ['] serve-key is key
   ['] serve-type is type
   webserver-task start-task
;

only forth definitions
web-interface
~ evaluate ; is web-interface

( Lazy loaded Server Terminal )

:noname [ ' web-interface >body @ ] literal execute
r|
also streams also sockets also WiFi also web-interface

: login ( z z -- )
   WIFI_MODE_STA Wifi.mode
   WiFi.begin begin WiFi.localIP 0= while 100 ms repeat WiFi.localIP ip. cr
   z" forth" MDNS.begin if ." MDNS started" else ." MDNS failed" then cr ;
: webui ( z z -- ) login 80 server ;

only forth definitions
web-interface
| evaluate ; is web-interface
: login web-interface forth r| login | evaluate ;
: webui web-interface forth r| webui | evaluate ;
vocabulary registers   registers definitions

( Tools for working with bit masks )
: m! ( val shift mask a -- )
   dup >r @ over invert and >r >r lshift r> and r> or r> ! ;
: m@ ( shift mask a -- val ) @ and swap rshift ;

only forth definitions
internals definitions
transfer timers-builtins
forth definitions

( Lazy loaded timers )
: timers r|

vocabulary timers   timers definitions
  also registers also interrupts also internals
transfer timers-builtins

$3ff5f000 constant TIMG_BASE
( group n = 0/1, timer x = 0/1, watchdog m = 0-5 )
: TIMGn ( n -- a ) $10000 * TIMG_BASE + ;
: TIMGn_Tx ( n x -- a ) $24 * swap TIMGn + ;
: TIMGn_TxCONFIG_REG ( n x -- a ) TIMGn_Tx 0 cells + ;
: TIMGn_TxLOHI_REG ( n x -- a ) TIMGn_Tx 1 cells + ;
: TIMGn_TxUPDATE_REG ( n x -- a ) TIMGn_Tx 3 cells + ;
: TIMGn_TxALARMLOHI_REG ( n x -- a ) TIMGn_Tx 4 cells + ;
: TIMGn_TxLOADLOHI_REG ( n x -- a ) TIMGn_Tx 6 cells + ;
: TIMGn_TxLOAD_REG ( n x -- a ) TIMGn_Tx 8 cells + ;

: TIMGn_Tx_WDTCONFIGm_REG ( n m -- a ) swap TIMGn cells + $48 + ;
: TIMGn_Tx_WDTFEED_REG ( n -- a ) TIMGn $60 + ;
: TIMGn_Tx_WDTWPROTECT_REG ( n -- a ) TIMGn $6c + ;

: TIMGn_RTCCALICFG_REG ( n -- a ) TIMGn $68 + ;
: TIMGn_RTCCALICFG1_REG ( n -- a ) TIMGn $6c + ;

: TIMGn_Tx_INT_ENA_REG ( n -- a ) TIMGn $98 + ;
: TIMGn_Tx_INT_RAW_REG ( n -- a ) TIMGn $9c + ;
: TIMGn_Tx_INT_ST_REG ( n -- a ) TIMGn $a0 + ;
: TIMGn_Tx_INT_CLR_REG ( n -- a ) TIMGn $a4 + ;

: t>nx ( t -- n x ) dup 2/ 1 and swap 1 and ;

: timer@ ( t -- lo hi )
   dup t>nx TIMGn_TxUPDATE_REG 0 swap !
       t>nx TIMGn_TxLOHI_REG 2@ ;
: timer! ( lo hi t -- )
   dup >r t>nx TIMGn_TxLOADLOHI_REG 2!
       r> t>nx TIMGn_TxLOAD_REG 0 swap ! ;
: alarm ( t -- a ) t>nx TIMGn_TxALARMLOHI_REG ;

: enable! ( v t ) >r 31 $80000000 r> t>nx TIMGn_TxCONFIG_REG m! ;
: increase! ( v t ) >r 30 $40000000 r> t>nx TIMGn_TxCONFIG_REG m! ;
: autoreload! ( v t ) >r 29 $20000000 r> t>nx TIMGn_TxCONFIG_REG m! ;
: divider! ( v t ) >r 13 $1fffc000 r> t>nx TIMGn_TxCONFIG_REG m! ;
: edgeint! ( v t ) >r 12 $1000 r> t>nx TIMGn_TxCONFIG_REG m! ;
: levelint! ( v t ) >r 11 $800 r> t>nx TIMGn_TxCONFIG_REG m! ;
: alarm-enable! ( v t ) >r 10 $400 r> t>nx TIMGn_TxCONFIG_REG m! ;
: alarm-enable@ ( v t ) >r 10 $400 r> t>nx TIMGn_TxCONFIG_REG m@ ;

: int-enable! ( f t -- )
   t>nx swap >r dup 1 swap lshift r> TIMGn_Tx_INT_ENA_REG m! ;

: onalarm ( xt t ) swap >r t>nx r> 0 ESP_INTR_FLAG_EDGE 0
                   timer_isr_register throw ;
: interval ( xt usec t ) 80 over divider!
                         swap over 0 swap alarm 2!
                         1 over increase!
                         1 over autoreload!
                         1 over alarm-enable!
                         1 over edgeint!
                         0 over 0 swap timer!
                         dup >r onalarm r>
                         1 swap enable! ;
: rerun ( t -- ) 1 swap alarm-enable! ;

only forth definitions
timers
| evaluate ;
( Lazy loaded Bluetooth Serial Terminal )
: bterm r|
vocabulary bterm  bterm definitions
also bluetooth also internals also esp
120000 getFreeHeap - 0 max relinquish ( must have 110k for bluetooth )
z" forth xx:xx:xx:xx:xx:xx" constant name
( Create unique name for device )
base @ hex getEfuseMac
<# # # char : hold # # #> name 6 + swap cmove
<# # # char : hold # # char : hold # # char : hold # # #> name c + swap cmove
base !
SerialBT.new constant bt
name 0 bt SerialBT.begin drop
." Bluetooth Serial Terminal on: " name z>s type cr
: bt-type ( a n -- ) bt SerialBT.write drop ;
: bt-key? ( -- f ) bt SerialBT.available 0<> pause ;
: bt-key ( -- ch ) begin bt-key? until 0 >r rp@ 1 bt SerialBT.readBytes drop r> ;
: bt-on  ['] bt-type is type      ['] bt-key is key      ['] bt-key? is key? ;
: bt-off ['] default-type is type ['] default-key is key ['] default-key? is key? ;
only forth definitions
bterm 500 ms bt-on
| evaluate ;
( Lazy loaded Telnet )
: telnetd r|

vocabulary telnetd   telnetd definitions also sockets

-1 value sockfd   -1 value clientfd
sockaddr telnet-port   sockaddr client   variable client-len

defer broker

: telnet-emit ( ch -- ) >r rp@ 1 clientfd write-file rdrop if broker then ;
: telnet-type ( a n -- ) for aft dup c@ telnet-emit 1+ then next drop ;
: telnet-key ( -- n ) 0 >r rp@ 1 clientfd read-file swap 1 <> or if rdrop broker then r> ;

: connection ( n -- )
  dup 0< if drop exit then to clientfd
  0 echo !
  ['] telnet-key is key
  ['] telnet-type is type quit ;

: wait-for-connection
  begin
    sockfd client client-len sockaccept
    dup 0 >= if exit else drop then
  again
;

: broker-connection
  rp0 rp! sp0 sp!
  begin
    ['] default-key is key   ['] default-type is type
    -1 echo !
    ." Listening on port " telnet-port ->port@ . cr
    wait-for-connection
    ." Connected: " dup . cr connection
  again ;
' broker-connection is broker

: server ( port -- )
  telnet-port ->port!
  AF_INET SOCK_STREAM 0 socket to sockfd
  sockfd non-block throw
  sockfd telnet-port sizeof(sockaddr_in) bind throw
  sockfd 1 listen throw   broker ;

only forth definitions
telnetd
| evaluate ;
internals definitions
transfer camera-builtins
forth definitions

( Lazy loaded camera handling for ESP32-CAM )
: camera r|

vocabulary camera   camera definitions
  also internals
transfer camera-builtins

0 constant PIXFORMAT_RGB565
1 constant PIXFORMAT_YUV422
2 constant PIXFORMAT_GRAYSCALE
3 constant PIXFORMAT_JPEG
4 constant PIXFORMAT_RGB888
5 constant PIXFORMAT_RAW
6 constant PIXFORMAT_RGB444
7 constant PIXFORMAT_RGB555

0 constant FRAMESIZE_96x96    ( 96x96)
1 constant FRAMESIZE_QQVGA    ( 160x120 )
2 constant FRAMESIZE_QCIF     ( 176x144 )
3 constant FRAMESIZE_HQVGA    ( 240x176 )
4 constant FRAMESIZE_240x240  ( 176x144 )
5 constant FRAMESIZE_QVGA     ( 320x240 )
6 constant FRAMESIZE_CIF      ( 400x296 )
7 constant FRAMESIZE_HVGA     ( 480x320 )
8 constant FRAMESIZE_VGA      ( 640x480 )
9 constant FRAMESIZE_SVGA     ( 800x600 )
10 constant FRAMESIZE_XGA     ( 1024x768 )
11 constant FRAMESIZE_HD      ( 1280x720 )
12 constant FRAMESIZE_SXGA    ( 1280x1024 )
13 constant FRAMESIZE_UXGA    ( 1600x1200 )

( See https://github.com/espressif/esp32-camera/blob/master/driver/include/esp_camera.h )
( Settings for AI_THINKER )
create camera-config
  32 , ( pin_pwdn ) -1 , ( pin_reset ) 0 , ( pin_xclk )
  26 , ( pin_sscb_sda ) 27 , ( pin_sscb_scl )
  35 , 34 , 39 , 36 , 21 , 19 , 18 , 5 , ( pin_d7 - pin_d0 )
  25 , ( pin_vsync ) 23 , ( pin_href ) 22 , ( pin_pclk )
  20000000 , ( xclk_freq_hz )
  0 , ( ledc_timer ) 0 , ( ledc_channel )
  here
  PIXFORMAT_JPEG , ( pixel_format )
  here
  FRAMESIZE_VGA , ( frame_size )
  here
  12 , ( jpeg_quality 0-63 low good )
  here
  1 , ( fb_count )
constant camera-fb-count
constant camera-jpeg-quality
constant camera-frame-size
constant camera-format

: field@ ( n -- n ) dup create , cell+ does> @ + @ ;

0
field@ fb->buf
field@ fb->len
field@ fb->width
field@ fb->height
field@ fb->format
field@ fb->sec
field@ fb->usec
drop

5 cells
field@ s->xclk_freq_hz ( a )
field@ s->init_status ( s )
field@ s->reset ( s )
field@ s->set_pixformat ( s pixformat )
field@ s->set_framesize ( s framesize )
field@ s->set_contrast ( s level )
field@ s->set_brightness ( s level )
field@ s->set_saturation ( s level )
field@ s->set_sharpness ( s level )
field@ s->set_denoise ( s level )
field@ s->set_gainceiling ( s gainceil )
field@ s->set_quality ( s quality )
field@ s->set_colorbar ( s enable )
field@ s->set_whitebal ( s enable )
field@ s->set_gain_ctrl ( s enable )
field@ s->set_exposure_ctrl ( s enable )
field@ s->set_hmirror ( s enable )
field@ s->set_vflip ( s enable )

field@ s->set_aec2 ( s enable )
field@ s->set_awb_gain ( s enable )
field@ s->set_agc_gain ( s gain )
field@ s->set_aec_value ( s gain )

field@ s->set_special_effect ( s effect )
field@ s->set_wb_mode ( s mode )
field@ s->set_ae_level ( s level )

field@ s->set_raw_gma ( s enable )
field@ s->set_lenc ( s enable )

field@ s->get_reg ( s reg mask )
field@ s->set_reg ( s reg mask value )
field@ s->set_res_raw ( s startX startY endX endY offsetX offsetY totalX totalY outputX outputY scale binning )
field@ s->set_pll ( s bypass mul sys root pre seld5 pclken pclk )
field@ s->set_xclk ( s time xclk )
drop

forth definitions
camera
| evaluate ;

[THEN]
( Lazy loaded Camera Server )
: camera-server r~

camera httpd
vocabulary camera-server   camera-server definitions
  also camera also httpd

r|
<!DOCTYPE html>
<body>
<img id="pic">
<script>
var pic = document.getElementById('pic');
function httpPost(url, callback) {
  var r = new XMLHttpRequest();
  r.responseType = 'blob';
  r.onreadystatechange = function() {
    if (this.readyState == XMLHttpRequest.DONE) {
      if (this.status === 200) {
        callback(this.response);
      } else {
        callback(null);
      }
    }
  };
  r.open('POST', url);
  r.send();
}
function Frame() {
  httpPost('./image', function(r) {
    if (r !== null) {
      try {
        pic.src = URL.createObjectURL(r);
      } catch (e) {
      }
    }
    setTimeout(Frame, 30);
  });
}
Frame();
</script>
| constant index-html# constant index-html

: handle-index
   s" text/html" ok-response
   index-html index-html# send
;

: handle-image
  s" image/jpeg" ok-response
  esp_camera_fb_get dup dup @ swap cell+ @ send
  esp_camera_fb_return
;

: handle1
  handleClient if
    s" /" path str= if handle-index exit then
    s" /image" path str= if handle-image exit then
    notfound-response
  then
;

: do-serve    begin ['] handle1 catch drop pause again ;

: server ( port -- )
   server
   camera-config esp_camera_init throw
   do-serve
;

only forth definitions
camera-server
~ evaluate ;
internals definitions

( Change default block source on arduino )
: arduino-default-use s" /spiffs/blocks.fb" open-blocks ;
' arduino-default-use is default-use

( Setup remember file )
: arduino-remember-filename   s" /spiffs/myforth" ;
' arduino-remember-filename is remember-filename

( Check for autoexec.fs and run if present.
  Failing that, try to revive save image. )
: autoexec
   300 for key? if rdrop exit then 10 ms next
   s" /spiffs/autoexec.fs" ['] included catch 2drop drop
   ['] revive catch drop ;
' autoexec ( leave on the stack for fini.fs )

forth definitions
internals definitions
( TODO: Figure out why this has to happen so late. )
transfer internals-builtins
forth definitions internals
( Bring a forth to the top of the vocabulary. )
transfer forth
( Move heap to save point, with a gap. )
setup-saving-base
forth
execute ( assumes an xt for autoboot is on the dstack )
ok
)""";

// Work around lack of ftruncate
static cell_t ResizeFile(cell_t fd, cell_t size) {
  struct stat st;
  char buf[256];
  cell_t t = fstat(fd, &st);
  if (t < 0) { return errno; }
  if (size < st.st_size) {
    // TODO: Implement truncation
    return ENOSYS;
  }
  cell_t oldpos = lseek(fd, 0, SEEK_CUR);
  if (oldpos < 0) { return errno; }
  t = lseek(fd, 0, SEEK_END);
  if (t < 0) { return errno; }
  memset(buf, 0, sizeof(buf));
  while (st.st_size < size) {
    cell_t len = sizeof(buf);
    if (size - st.st_size < len) {
      len = size - st.st_size;
    }
    t = write(fd, buf, len);
    if (t != len) {
      return errno;
    }
    st.st_size += t;
  }
  t = lseek(fd, oldpos, SEEK_SET);
  if (t < 0) { return errno; }
  return 0;
}

#ifdef ENABLE_INTERRUPTS_SUPPORT
struct handle_interrupt_args {
  cell_t xt;
  cell_t arg;
};

static void IRAM_ATTR HandleInterrupt(void *arg) {
  struct handle_interrupt_args *args = (struct handle_interrupt_args *) arg;
  cell_t code[2];
  code[0] = args->xt;
  code[1] = g_sys->YIELD_XT;
  cell_t fstack[INTERRUPT_STACK_CELLS];
  cell_t rstack[INTERRUPT_STACK_CELLS];
  cell_t stack[INTERRUPT_STACK_CELLS];
  stack[0] = args->arg;
  cell_t *rp = rstack;
  *++rp = (cell_t) (fstack + 1);
  *++rp = (cell_t) (stack + 1);
  *++rp = (cell_t) code;
  forth_run(rp);
}

static cell_t EspIntrAlloc(cell_t source, cell_t flags, cell_t xt, cell_t arg, void *ret) {
  // NOTE: Leaks memory.
  struct handle_interrupt_args *args = (struct handle_interrupt_args *) malloc(sizeof(struct handle_interrupt_args));
  args->xt = xt;
  args->arg = arg;
  return esp_intr_alloc(source, flags, HandleInterrupt, args, (intr_handle_t *) ret);
}

static cell_t GpioIsrHandlerAdd(cell_t pin, cell_t xt, cell_t arg) {
  // NOTE: Leaks memory.
  struct handle_interrupt_args *args = (struct handle_interrupt_args *) malloc(sizeof(struct handle_interrupt_args));
  args->xt = xt;
  args->arg = arg;
  return gpio_isr_handler_add((gpio_num_t) pin, HandleInterrupt, args);
}

static cell_t TimerIsrRegister(cell_t group, cell_t timer, cell_t xt, cell_t arg, cell_t flags, void *ret) {
  // NOTE: Leaks memory.
  struct handle_interrupt_args *args = (struct handle_interrupt_args *) malloc(sizeof(struct handle_interrupt_args));
  args->xt = xt;
  args->arg = arg;
  return timer_isr_register((timer_group_t) group, (timer_idx_t) timer, HandleInterrupt, args, flags, (timer_isr_handle_t *) ret);
}
#endif
void setup() {
  cell_t fh = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  cell_t hc = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
  if (fh - hc < MINIMUM_FREE_SYSTEM_HEAP) {
    hc = fh - MINIMUM_FREE_SYSTEM_HEAP;
  }
  cell_t *heap = (cell_t *) malloc(hc);
  forth_init(0, 0, heap, hc, boot, sizeof(boot));
}

void loop() {
  g_sys->rp = forth_run(g_sys->rp);
}
