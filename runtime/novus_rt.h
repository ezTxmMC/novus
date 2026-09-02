/*
 * novus_rt.h - the C runtime embedded into every program that novusc emits.
 *
 * Values are dynamically typed (NvVal): integers, floats, bools, strings,
 * arrays, maps, class instances and enum constants all flow through the
 * same variables. Integers up to 62 bits are encoded in the pointer itself
 * (no allocation); everything else lives in a garbage collected heap
 * (see nv_memory.h) that hands memory back to the system as objects die.
 *
 * The runtime is one translation unit split into parts, one per subsystem,
 * included below in dependency order. `novusc` pastes them into every
 * generated C file in that same order (tools/embed.nv inlines the quoted
 * includes), so a program stays a single self-contained C file; with
 * `novusc build --no-runtime` the generated file includes this header
 * instead and the parts are picked up from this directory.
 *
 * Portable C11 (anonymous unions): builds with gcc, clang, zig cc and
 * mingw on 64-bit Linux, macOS and Windows.
 */
#ifndef NOVUS_RT_H
#define NOVUS_RT_H

#include "nv_platform.h"   /* system headers, feature macros, platform shims */
#include "nv_values.h"     /* NvVal, arrays, maps, objects, classes */
#include "nv_memory.h"     /* allocator, garbage collector, roots, threads registry */
#include "nv_data.h"       /* string builder, errors, constructors, arrays, maps */
#include "nv_display.h"    /* type names, display, coercion */
#include "nv_ops.h"        /* operators, fast paths, indexing, members */
#include "nv_classes.h"    /* classes, objects, enums */
#include "nv_strings.h"    /* string helpers */
#include "nv_invoke.h"     /* method calls on any value, iteration */
#include "nv_io.h"         /* console I/O, files, builtins, program start */
#include "nv_path.h"       /* path module */
#include "nv_os.h"         /* os module */
#include "nv_std.h"        /* std natives: math, time, random, fmt, hash, io */
#include "nv_http.h"       /* http module */
#include "nv_json.h"       /* json module */
#include "nv_bits.h"       /* bitwise operations */
#include "nv_bytes.h"      /* binary buffers */
#include "nv_net.h"        /* TCP sockets */
#include "nv_zlib.h"       /* zlib / deflate */
#include "nv_crypto.h"     /* SHA-1, MD5, AES, random bytes */
#include "nv_rsa.h"        /* RSA */
#include "nv_threads.h"    /* threads, virtual threads, tasks, locks, channels */

#endif /* NOVUS_RT_H */
