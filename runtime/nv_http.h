/* nv_http.h - the http module (driven by curl). Part of the Novus runtime; included by novus_rt.h. */
#ifndef NV_HTTP_H
#define NV_HTTP_H

/* ------------------------------------------------------------------ */
/* http module - driven by the curl command line tool                  */
/* ------------------------------------------------------------------ */

static nv nv_json_stringify(nv v);

static const char *nv_shell_quote(const char *s) {
    NvSb sb;
    nv_sb_init(&sb);
#ifdef _WIN32
    nv_sb_addc(&sb, '"');
    for (; *s; s++) {
        if (*s == '"') {
            nv_sb_add(&sb, "\\\"");
        } else {
            nv_sb_addc(&sb, *s);
        }
    }
    nv_sb_addc(&sb, '"');
#else
    nv_sb_addc(&sb, '\'');
    for (; *s; s++) {
        if (*s == '\'') {
            nv_sb_add(&sb, "'\\''");
        } else {
            nv_sb_addc(&sb, *s);
        }
    }
    nv_sb_addc(&sb, '\'');
#endif
    return nv_sb_finish(&sb);
}

static int nv_http_counter = 0;

static nv nv_http_temp_name(const char *what) {
    char buf[64];
    sprintf(buf, "novus-http-%lld-%d-%s", (long long)NV_GETPID(), nv_http_counter++, what);
    return nv_path_join(2, nv_path_temp(), nv_str(buf));
}

/* Parses "Key: value" lines of a dumped header block into a map. */
static nv nv_http_parse_headers(nv text) {
    nv lines = nv_str_split(text, nv_str("\n"));
    nv out = nv_map();
    int i;
    for (i = 0; i < lines->a->len; i++) {
        nv line = nv_str_trim(lines->a->items[i]);
        const char *s = nv_cstr(line);
        const char *colon = strchr(s, ':');
        if (!colon || strncmp(s, "HTTP/", 5) == 0) {
            continue;
        }
        {
            nv key = nv_str_trim(nv_strn(s, (int)(colon - s)));
            nv val = nv_str_trim(nv_str(colon + 1));
            nv_map_set(out->m, nv_cstr(nv_str_case(key, 0)), val);
        }
    }
    return out;
}

/* {status, ok, body, headers, error} - never aborts on transport errors. */
static nv nv_http_request(nv method, nv url, nv body, nv headers) {
    nv outFile = nv_http_temp_name("body");
    nv hdrFile = nv_http_temp_name("headers");
    nv errFile = nv_http_temp_name("stderr");
    nv bodyFile = 0;
    nv result = nv_map();
    nv statusText;
    NvSb cmd;
    int hasContentType = 0, i;
    nv_sb_init(&cmd);
    nv_sb_add(&cmd, "curl -s -S -L --max-redirs 10 -X ");
    nv_sb_add(&cmd, nv_shell_quote(nv_cstr(nv_str_case(nv_to_str(method), 1))));
    nv_sb_add(&cmd, " --output ");
    nv_sb_add(&cmd, nv_shell_quote(nv_cstr(outFile)));
    nv_sb_add(&cmd, " --dump-header ");
    nv_sb_add(&cmd, nv_shell_quote(nv_cstr(hdrFile)));
    nv_sb_add(&cmd, " --stderr ");
    nv_sb_add(&cmd, nv_shell_quote(nv_cstr(errFile)));
    nv_sb_add(&cmd, " --write-out ");
    nv_sb_add(&cmd, nv_shell_quote("%{http_code}"));
    if (headers && nv_type_of(headers) == NV_MAP) {
        nv_map_order(headers->m);
        for (i = 0; i < headers->m->len; i++) {
            nv line = nv_concat(nv_concat(nv_str(headers->m->items[i].key), nv_str(": ")), headers->m->items[i].val);
            if (strcmp(nv_cstr(nv_str_case(nv_str(headers->m->items[i].key), 0)), "content-type") == 0) {
                hasContentType = 1;
            }
            nv_sb_add(&cmd, " -H ");
            nv_sb_add(&cmd, nv_shell_quote(nv_cstr(line)));
        }
    }
    if (body && nv_type_of(body) != NV_NULL && !(nv_type_of(body) == NV_STR && body->slen == 0)) {
        nv text = body;
        if (nv_type_of(body) == NV_MAP || nv_type_of(body) == NV_ARR || nv_type_of(body) == NV_OBJ) {
            text = nv_json_stringify(body);
            if (!hasContentType) {
                nv_sb_add(&cmd, " -H ");
                nv_sb_add(&cmd, nv_shell_quote("Content-Type: application/json"));
            }
        }
        bodyFile = nv_http_temp_name("request");
        nv_write_file(bodyFile, text);
        nv_sb_add(&cmd, " --data-binary @");
        nv_sb_add(&cmd, nv_shell_quote(nv_cstr(bodyFile)));
    }
    nv_sb_add(&cmd, " ");
    nv_sb_add(&cmd, nv_shell_quote(nv_display(url)));
    {
        int len = cmd.len;
        nv command = nv_str_own(nv_sb_finish(&cmd), len);
        statusText = nv_str_trim(nv_os_output(command));
    }
    {
        long long status = atoll(nv_cstr(statusText));
        nv error = nv_str_trim(nv_read_file(errFile));
        if (statusText->slen == 0) {
            error = nv_str("http: could not run curl (is it installed and in PATH?)");
        }
        nv_map_set(result->m, "status", nv_int(status));
        nv_map_set(result->m, "ok", nv_bool(status >= 200 && status < 300));
        nv_map_set(result->m, "body", nv_read_file(outFile));
        nv_map_set(result->m, "headers", nv_http_parse_headers(nv_read_file(hdrFile)));
        nv_map_set(result->m, "error", status == 0 ? (error->slen ? error : nv_str("http: request failed")) : nv_str(""));
    }
    remove(nv_cstr(outFile));
    remove(nv_cstr(hdrFile));
    remove(nv_cstr(errFile));
    if (bodyFile) {
        remove(nv_cstr(bodyFile));
    }
    return result;
}

static nv nv_http_simple(const char *method, nv url, nv body) {
    nv r = nv_http_request(nv_str(method), url, body, nv_map());
    nv error = nv_map_get(r->m, "error");
    if (error->slen) {
        nv_error("%s (%s %s)", nv_cstr(error), method, nv_display(url));
    }
    return nv_map_get(r->m, "body");
}

static nv nv_http_get(nv url) { return nv_http_simple("GET", url, nv_nil); }
static nv nv_http_post(nv url, nv body) { return nv_http_simple("POST", url, body); }
static nv nv_http_put(nv url, nv body) { return nv_http_simple("PUT", url, body); }
static nv nv_http_delete(nv url) { return nv_http_simple("DELETE", url, nv_nil); }

static nv nv_http_download(nv url, nv file) {
    nv r = nv_http_request(nv_str("GET"), url, nv_nil, nv_map());
    if (!nv_truthy(nv_map_get(r->m, "ok"))) {
        return nv_bool(0);
    }
    nv_write_file(file, nv_map_get(r->m, "body"));
    return nv_bool(1);
}


#endif /* NV_HTTP_H */
