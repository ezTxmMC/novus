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
#include "nv_sexp.h"       /* s-expression navigation for the compiler */
#include "nv_invoke.h"     /* method calls on any value, iteration */
#include "nv_typed.h"      /* unboxed numbers of typed code: declared fields, mixed operands */
#include "nv_io.h"         /* console I/O, files, builtins, program start */
#include "nv_path.h"       /* path module */
#include "nv_os.h"         /* os module (nv_shell.h, nv_os_stat.h, nv_os_fs.h, nv_os_proc.h) */
#include "nv_std.h"        /* std natives: math, time, random, fmt, hash, io */
#include "nv_stdin.h"      /* io.readBytes, io.eof */
#include "nv_http.h"       /* http module */
#include "nv_utf8.h"       /* UTF-8 decoding and encoding */
#include "nv_unicode.h"    /* UTF-8 code points and UTF-16 positions */
#include "nv_json.h"       /* json module (nv_json_*.h) */
#include "nv_bits.h"       /* bitwise operations */
#include "nv_bytes.h"      /* binary buffers */
#include "nv_net.h"        /* TCP sockets */
#include "nv_zlib.h"       /* zlib / deflate */
#include "nv_crypto.h"     /* SHA-1, MD5, AES, random bytes */
#include "nv_rsa.h"        /* RSA */
#include "nv_threads.h"    /* threads, virtual threads, tasks, locks, channels */

#endif /* NOVUS_RT_H */
