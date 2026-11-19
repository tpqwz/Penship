#ifndef PENSHIP_LINKURL_H
#define PENSHIP_LINKURL_H

#include <stddef.h>
#include <string.h>

/* A bare http(s) address, or a www address, from the GFM autolink extension.
   The caller paints the matched bytes and, for www, opens it with an http:// prefix. */

static int link_alnum(unsigned char c) {
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

static int link_host(unsigned char c) {
    return link_alnum(c) || c == '_' || c == '-';
}

static int link_boundary(const char *s, size_t i) {
    unsigned char p;
    if (i == 0) {
        return 1;
    }
    p = (unsigned char)s[i - 1];
    return p == ' ' || p == '\t' || p == '*' || p == '_' || p == '~' || p == '(';
}

/* One past the domain, or i when the labels are not a domain.
   The last two labels cannot contain '_'. A domain needs a dot. */
static size_t link_domain(const char *s, size_t n, size_t i) {
    size_t p = i;
    int dots = 0;
    int prev_us = 0;
    int last_us = 0;
    if (p >= n || !link_host((unsigned char)s[p])) {
        return i;
    }
    while (p < n && link_host((unsigned char)s[p])) {
        if (s[p] == '_') {
            last_us = 1;
        }
        p++;
    }
    while (p + 1 < n && s[p] == '.' && link_host((unsigned char)s[p + 1])) {
        dots++;
        prev_us = last_us;
        last_us = 0;
        p++;
        while (p < n && link_host((unsigned char)s[p])) {
            if (s[p] == '_') {
                last_us = 1;
            }
            p++;
        }
    }
    if (dots < 1 || prev_us || last_us) {
        return i;
    }
    return p;
}

/* Drop trailing punctuation. A ')' stays while it balances a '(' in the link.
   An &name; entity at the end is not part of the link. floor is the domain end. */
static size_t link_trim(const char *s, size_t begin, size_t end, size_t floor) {
    size_t open_n = 0;
    size_t close_n = 0;
    size_t k;
    for (k = begin; k < end; k++) {
        if (s[k] == '(') {
            open_n++;
        } else if (s[k] == ')') {
            close_n++;
        }
    }
    while (end > floor) {
        unsigned char c = (unsigned char)s[end - 1];
        if (c == ')') {
            if (close_n <= open_n) {
                break;
            }
            close_n--;
            end--;
            continue;
        }
        if (c == '?' || c == '!' || c == '.' || c == ',' || c == ':' || c == '*' || c == '_' || c == '~') {
            end--;
            continue;
        }
        if (c == ';') {
            size_t t = end - 1;
            while (t > begin && link_alnum((unsigned char)s[t - 1])) {
                t--;
            }
            if (t < end - 1 && t > begin && s[t - 1] == '&') {
                end = t - 1;
                if (end < floor) {
                    end = floor;
                }
                continue;
            }
        }
        break;
    }
    return end;
}

/* 1 when s[i] starts a bare link. *end is one past the visible text. *www is 1 for a www address. */
static int link_extended(const char *s, size_t n, size_t i, size_t *end, int *www) {
    size_t scheme = 0;
    size_t dom = 0;
    size_t p = 0;
    int is_www = 0;
    if (s == NULL || end == NULL || www == NULL || i >= n || !link_boundary(s, i)) {
        return 0;
    }
    if (i + 8 <= n && memcmp(s + i, "https://", 8) == 0) {
        scheme = 8;
    } else if (i + 7 <= n && memcmp(s + i, "http://", 7) == 0) {
        scheme = 7;
    } else if (i + 4 <= n && memcmp(s + i, "www.", 4) == 0) {
        scheme = 4;
        is_www = 1;
    } else {
        return 0;
    }
    dom = link_domain(s, n, i + scheme);
    if (dom == i + scheme) {
        return 0;
    }
    p = dom;
    while (p < n && s[p] != ' ' && s[p] != '\t' && s[p] != '<') {
        p++;
    }
    p = link_trim(s, i, p, dom);
    if (p < dom) {
        return 0;
    }
    *end = p;
    *www = is_www;
    return 1;
}

static int link_mail_local(unsigned char c) {
    return link_alnum(c) || c == '.' || c == '-' || c == '_' || c == '+';
}

static int link_mail_label(unsigned char c) {
    return link_alnum(c) || c == '-' || c == '_';
}

/* 1 when s[i] starts an email. *end is one past the visible text.
   *scheme is 1 when that text already begins with mailto:. A trailing dot stays outside.
   The domain needs a dot, and it cannot end in '-' or '_'. '+' belongs before the '@'. */
static int link_email(const char *s, size_t n, size_t i, size_t *end, int *scheme) {
    size_t at = 0;
    size_t p = 0;
    int dots = 0;
    int have = 0;
    if (s == NULL || end == NULL || scheme == NULL || i >= n) {
        return 0;
    }
    if (i + 7 <= n && memcmp(s + i, "mailto:", 7) == 0) {
        if (i > 0 && link_alnum((unsigned char)s[i - 1])) {
            return 0;
        }
        have = 1;
        i += 7;
    } else if (i > 0 && link_mail_local((unsigned char)s[i - 1])) {
        return 0;
    }
    if (i >= n || !link_mail_local((unsigned char)s[i])) {
        return 0;
    }
    at = i;
    while (at < n && link_mail_local((unsigned char)s[at])) {
        at++;
    }
    if (at == i || at >= n || s[at] != '@') {
        return 0;
    }
    p = at + 1;
    if (p >= n || !link_mail_label((unsigned char)s[p])) {
        return 0;
    }
    while (p < n && link_mail_label((unsigned char)s[p])) {
        p++;
    }
    while (p + 1 < n && s[p] == '.' && link_mail_label((unsigned char)s[p + 1])) {
        dots++;
        p++;
        while (p < n && link_mail_label((unsigned char)s[p])) {
            p++;
        }
    }
    if (dots < 1 || !link_alnum((unsigned char)s[p - 1])) {
        return 0;
    }
    *end = p;
    *scheme = have;
    return 1;
}

#endif
