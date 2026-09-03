/* nv_std.h - std natives: math, time, random, fmt, hash, io, arrays. Part of the Novus runtime; included by novus_rt.h. */
#ifndef NV_STD_H
#define NV_STD_H

/* ------------------------------------------------------------------ */
/* std natives: math, time, random, fmt, hash, io, arrays              */
/* ------------------------------------------------------------------ */

static nv nv_math_sqrt(nv x) { return nv_float(sqrt(nv_as_double(x))); }
static nv nv_math_pow(nv b, nv e) { return nv_float(pow(nv_as_double(b), nv_as_double(e))); }
static nv nv_math_floor(nv x) { return nv_int((long long)floor(nv_as_double(x))); }
/* The nearest value a 32 bit float can hold. A program that has to agree
 * with one written in a language whose floats are 32 bit - a file format, a
 * protocol, another implementation of the same arithmetic - needs the
 * narrowing to happen where that program has it. */
static nv nv_math_to_float32(nv x) { return nv_float((double)(float)nv_as_double(x)); }
static nv nv_math_ceil(nv x) { return nv_int((long long)ceil(nv_as_double(x))); }
static nv nv_math_round(nv x) { return nv_int((long long)floor(nv_as_double(x) + 0.5)); }
static nv nv_math_sin(nv x) { return nv_float(sin(nv_as_double(x))); }
static nv nv_math_cos(nv x) { return nv_float(cos(nv_as_double(x))); }
static nv nv_math_tan(nv x) { return nv_float(tan(nv_as_double(x))); }
static nv nv_math_atan2(nv y, nv x) { return nv_float(atan2(nv_as_double(y), nv_as_double(x))); }
static nv nv_math_log(nv x) { return nv_float(log(nv_as_double(x))); }
static nv nv_math_exp(nv x) { return nv_float(exp(nv_as_double(x))); }

static struct tm *nv_gmtime(nv unixSeconds, struct tm *out) {
    time_t t = (time_t)nv_as_int(unixSeconds);
    struct tm *g = gmtime(&t);
    if (g) {
        *out = *g;
    } else {
        memset(out, 0, sizeof(*out));
    }
    return out;
}

static nv nv_time_format(nv unixSeconds, nv layout) {
    char buf[256];
    struct tm tm;
    nv_gmtime(unixSeconds, &tm);
    if (strftime(buf, sizeof(buf), nv_display(layout), &tm) == 0) {
        buf[0] = 0;
    }
    return nv_str(buf);
}

static nv nv_time_iso(nv unixSeconds) { return nv_time_format(unixSeconds, nv_str("%Y-%m-%dT%H:%M:%SZ")); }

static nv nv_time_parts(nv unixSeconds) {
    struct tm tm;
    nv m = nv_map();
    nv_gmtime(unixSeconds, &tm);
    nv_map_set_static(m->m, "year", nv_int(tm.tm_year + 1900));
    nv_map_set_static(m->m, "month", nv_int(tm.tm_mon + 1));
    nv_map_set_static(m->m, "day", nv_int(tm.tm_mday));
    nv_map_set_static(m->m, "hour", nv_int(tm.tm_hour));
    nv_map_set_static(m->m, "minute", nv_int(tm.tm_min));
    nv_map_set_static(m->m, "second", nv_int(tm.tm_sec));
    nv_map_set_static(m->m, "weekday", nv_int(tm.tm_wday));
    nv_map_set_static(m->m, "yearday", nv_int(tm.tm_yday + 1));
    return m;
}

/* Per thread, so two threads drawing numbers never step on one another -
 * and so seed() makes the thread that calls it reproducible. */
static NV_TLS unsigned long long nv_rng_state = 0;

static nv nv_random_seed(nv value) {
    nv_rng_state = (unsigned long long)nv_as_int(value) * 2654435761ULL + 88172645463325252ULL;
    return nv_nil;
}

static nv nv_random_next(void) {
    unsigned long long x;
    if (nv_rng_state == 0) {
        nv_random_seed(nv_int((long long)time(0) ^ (long long)NV_GETPID()));
    }
    x = nv_rng_state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    nv_rng_state = x;
    return nv_int((long long)(x >> 2));
}

static nv nv_fmt_fixed(nv x, nv decimals) {
    char buf[64];
    int d = (int)nv_as_int(decimals);
    if (d < 0) {
        d = 0;
    }
    if (d > 20) {
        d = 20;
    }
    sprintf(buf, "%.*f", d, nv_as_double(x));
    return nv_str(buf);
}

static nv nv_hash_fnv1a(nv text) {
    const unsigned char *p = (const unsigned char *)nv_display(text);
    unsigned long long h = 14695981039346656037ULL;
    for (; *p; p++) {
        h ^= *p;
        h *= 1099511628211ULL;
    }
    return nv_int((long long)(h >> 2));
}

static nv nv_hash_crc32(nv text) {
    const unsigned char *p = (const unsigned char *)nv_display(text);
    unsigned int crc = 0xFFFFFFFFu;
    for (; *p; p++) {
        int k;
        crc ^= *p;
        for (k = 0; k < 8; k++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return nv_int((long long)(crc ^ 0xFFFFFFFFu));
}

static nv nv_read_all(void) {
    NvSb sb;
    char buf[4096];
    size_t n;
    int len;
    nv_sb_init(&sb);
    while ((n = fread(buf, 1, sizeof(buf), stdin)) > 0) {
        nv_sb_addn(&sb, buf, (int)n);
    }
    len = sb.len;
    return nv_str_own(nv_sb_finish(&sb), len);
}

static nv nv_io_write(nv text) {
    fputs(nv_display(text), stdout);
    return nv_nil;
}

static nv nv_io_write_err(nv text) {
    fputs(nv_display(text), stderr);
    return nv_nil;
}

static nv nv_io_flush(void) {
    fflush(stdout);
    fflush(stderr);
    return nv_nil;
}

static int nv_sort_cmp(const void *a, const void *b) {
    nv l = *(const nv *)a;
    nv r = *(const nv *)b;
    if (nv_is_num(l) && nv_is_num(r)) {
        double x = nv_as_double(l), y = nv_as_double(r);
        return x < y ? -1 : (x > y ? 1 : 0);
    }
    return strcmp(nv_data(l), nv_data(r));
}

/* Specialised comparators: no type dispatch and no string materialisation
 * per comparison, which is most of the work when sorting a large array. */
static int nv_sort_cmp_tagged(const void *a, const void *b) {
    long long x = nv_ival(*(const nv *)a);
    long long y = nv_ival(*(const nv *)b);
    return x < y ? -1 : (x > y ? 1 : 0);
}

static int nv_sort_cmp_text(const void *a, const void *b) {
    return strcmp(nv_cstr(*(const nv *)a), nv_cstr(*(const nv *)b));
}

/* Introsort over raw 64-bit values: no indirect call per comparison, which
 * is what makes the generic qsort path slow for large integer arrays. */
static void nv_sort_ll(long long *values, int left, int right) {
    while (right - left > 12) {
        long long pivot, tmp;
        int i = left, j = right, middle = left + (right - left) / 2;
        if (values[middle] < values[left]) {
            tmp = values[middle]; values[middle] = values[left]; values[left] = tmp;
        }
        if (values[right] < values[left]) {
            tmp = values[right]; values[right] = values[left]; values[left] = tmp;
        }
        if (values[right] < values[middle]) {
            tmp = values[right]; values[right] = values[middle]; values[middle] = tmp;
        }
        pivot = values[middle];
        while (i <= j) {
            while (values[i] < pivot) {
                i++;
            }
            while (values[j] > pivot) {
                j--;
            }
            if (i <= j) {
                tmp = values[i]; values[i] = values[j]; values[j] = tmp;
                i++; j--;
            }
        }
        /* recurse into the smaller side, loop on the larger one */
        if (j - left < right - i) {
            nv_sort_ll(values, left, j);
            left = i;
        } else {
            nv_sort_ll(values, i, right);
            right = j;
        }
    }
    {
        int i;
        for (i = left + 1; i <= right; i++) {
            long long value = values[i];
            int j = i - 1;
            while (j >= left && values[j] > value) {
                values[j + 1] = values[j];
                j--;
            }
            values[j + 1] = value;
        }
    }
}

/* A sorted copy (numbers numerically, everything else by text). */
static nv nv_arr_sorted(nv a) {
    nv out;
    int i, tagged = 1, text = 1;
    if (nv_type_of(a) != NV_ARR) {
        nv_error("sort() needs an array");
    }
    out = nv_new(NV_ARR);
    out->a = nv_arr_with_capacity(a->a->len);
    memcpy(out->a->items, a->a->items, sizeof(nv) * (size_t)a->a->len);
    out->a->len = a->a->len;
    for (i = 0; i < out->a->len; i++) {
        nv value = out->a->items[i];
        if (!nv_is_tagged(value)) {
            tagged = 0;
            if (nv_type_of(value) != NV_STR) {
                text = 0;
            }
        } else {
            text = 0;
        }
        if (!tagged && !text) {
            break;
        }
    }
    if (out->a->len > 1) {
        if (tagged) {
            /* unbox, sort as plain integers, tag again */
            long long *values = (long long *)malloc(sizeof(long long) * (size_t)out->a->len);
            if (!values) {
                nv_error("out of memory");
            }
            for (i = 0; i < out->a->len; i++) {
                values[i] = nv_ival(out->a->items[i]);
            }
            nv_sort_ll(values, 0, out->a->len - 1);
            for (i = 0; i < out->a->len; i++) {
                out->a->items[i] = nv_int(values[i]);
            }
            free(values);
            return out;
        }
        qsort(out->a->items, (size_t)out->a->len, sizeof(nv), text ? nv_sort_cmp_text : nv_sort_cmp);
    }
    return out;
}


#endif /* NV_STD_H */
