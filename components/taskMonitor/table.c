#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <limits.h>
#include <assert.h>

#include "table.h"

#define CPSTART(c) (((unsigned char) (c)) >> (CHAR_BIT - 2) != 2)


static char const *DELIM = "\x1f";
static char const CORNER = '+';
static char const INTERSECT = '*';

static inline size_t max(size_t a, size_t b)
{
        return a > b ? a : b;
}

static size_t utf8len(char const *s)
{
        size_t len = 0;
        char const *c;

        for (c = s; *c; ++c) {
                if (CPSTART(*c)) {
                        len += 1;
                }
        }

        return len;
}

static size_t b2cp(char const *s, size_t n)
{
        size_t cp, i;

        for (cp = i = 0; s[i] && n > 0; ++i, --n) {
                if (CPSTART(s[i])) {
                        cp += 1;
                }
        }

        return cp;
}

static void fputnc(int c, size_t n, FILE *f)
{
        while (n --> 0) {
                fputc(c, f);
        }
}

static size_t find_break(char const *s, size_t max, bool *hyphen)
{
        char *c;
        size_t brk, cp;

        if (!*s) {
                *hyphen = false;
                return 0;
        }

        for (c = s, cp = brk = 0; *c && cp < max; ++c) {
                brk += 1;
                if (CPSTART(*c)) {
                        cp += 1;
                }
        }

        while (!CPSTART(*c)) {
                c += 1;
                brk += 1;
        }

        while (*c && c != s && !isspace(*c)) {
                c -= 1;
        }

        if (c == s) {
                *hyphen = true;
                while (!CPSTART(s[--brk]));
                return brk;
        }

        *hyphen = false;

        return c - s;
}

static void print_row(char * const *data, size_t *max, size_t *remaining, size_t cols, FILE *f)
{
        static size_t alloc = 0;
        static char const **cells = NULL;

        size_t i, n, pad;
        bool hyphen, finished;

        if (cols > alloc) {
                char const **tmp;
                alloc = cols;
                tmp = realloc(cells, alloc * sizeof *cells);
                if (tmp == NULL) {
                        return;
                }

                cells = tmp;

        }

        for (i = 0; i < cols; ++i) {
                cells[i] = data[i];
                remaining[i] = utf8len(cells[i]);
        }

        for (finished = false; !finished;) {
                finished = true;
                for (i = 0; i < cols; ++i) {
                        fputs("| ", f);

                        n = find_break(cells[i], max[i], &hyphen);
                        fwrite(cells[i], 1, n, f);

                        if (hyphen) {
                                fputc('-', f);
                        } else if (isspace(cells[i][n])) {
                                fputc(' ', f);
                                remaining[i] -= 1;
                                cells[i] += 1;
                        } else {
                                fputc(' ', f);
                        }

                        remaining[i] -= b2cp(cells[i], n);

                        if (remaining[i] != 0) {
                                finished = false;
                        }

                        pad = max[i] - b2cp(cells[i], n);
                        fputnc(' ', pad, f);

                        cells[i] += n;

                        if (i + 1 == cols) {
                                fputs("|\n", f);
                        }
                }
        }
}

bool table_init(struct table *t, ...)
{
        va_list ap;
        char *header, *fmt, *tmp, **tmph;
        size_t i, fmtlen, totalfmtlen;

        totalfmtlen = 0;
        
        t->rows     = 0;
        t->cols     = 0;
        t->alloc    = 0;
        t->data     = NULL;
        t->fmt      = NULL;
        t->headers  = NULL;

        va_start(ap, t);

        for (;; ++t->cols) {
                header = va_arg(ap, char *);
                if (header == NULL) {
                        break;
                }
                fmt    = va_arg(ap, char *);

                tmph = realloc(t->headers, (t->cols + 1) * sizeof (char *));
                if (tmph == NULL) {
                        free(t->headers);
                        return false;
                }
                t->headers = tmph;

                t->headers[t->cols] = header;

                fmtlen = strlen(fmt);
                tmp = realloc(t->fmt, totalfmtlen + fmtlen + 2);
                if (tmp == NULL) {
                        free(t->fmt);
                        free(t->headers);
                        return false;
                }
                t->fmt = tmp;
                strcpy(t->fmt + totalfmtlen, fmt);
                totalfmtlen += fmtlen;
                strcpy(t->fmt + totalfmtlen, DELIM);
                totalfmtlen += 1;
        }

        va_end(ap);

        t->max = malloc(t->cols * sizeof *t->max);
        if (t->max == NULL) {
                free(t->headers);
                free(t->fmt);
                return false;
        }

        for (i = 0; i < t->cols; ++i) {
                t->max[i] = utf8len(t->headers[i]);
        }

        return true;
}

bool table_add(struct table *t, ...)
{
        va_list ap;
        char *field, **row, buffer[512];
        size_t i;

        if (t->rows == t->alloc) {
                char ***tmp;
                t->alloc = t->alloc * 2 + 4;
                tmp      = realloc(t->data, t->alloc * sizeof (char **));
                if (tmp == NULL) {
                        return false;
                }
                t->data = tmp;
        }

        row = (t->data[t->rows++] = malloc(t->cols * sizeof (char *)));

        if (row == NULL) {
                t->rows -= 1;
                return false;
        }



        va_start(ap, t);
        vsnprintf(buffer, sizeof buffer, t->fmt, ap);
        va_end(ap);

        field = strtok(buffer, DELIM);

        for (i = 0; field != NULL; ++i, field = strtok(NULL, DELIM)) {
                assert(&row[i] == &t->data[t->rows - 1][i]);
                row[i] = malloc(strlen(field) + 1);
                if (row[i] == NULL) {
                        goto err;
                }
                strcpy(row[i], field);

                t->max[i] = max(t->max[i], utf8len(field));
        }

        return true;

err:
        while (i --> 0) {
                free(row[i]);
        }
        
        return false;
}

bool table_print(struct table const *t, size_t n, FILE *f)
{
        size_t i, width, avg, trimthrshld, *max, *remaining;

        if (n < t->cols * 3 + 4) {
                /* Not enough space */
                return false;
        }

        n -= 2;

        max = malloc(t->cols * sizeof *max);
        if (max == NULL) {
                return false;
        }

        remaining = malloc(t-> cols * sizeof *remaining);
        if (remaining == NULL) {
                free(max);
                return false;
        }

        width = t->cols * 3 + 1;
        for (i = 0; i < t->cols; ++i) {
                max[i] = t->max[i];
                width += t->max[i];
        }

        avg = n / t->cols;
        trimthrshld = 0;

        while (width > n) {
                bool none = true;
                for (i = 0; i < t->cols; ++i) {
                        if (max[i] + trimthrshld > avg) {
                                max[i] -= 1;
                                width -= 1;
                                none = false;
                        }
                }

                if (none) {
                        trimthrshld += 1;
                }
        }

        fputc(CORNER, f);
        fputnc('-', width - 2, f);
        fputc(CORNER, f);
        fputc('\n', f);

        print_row(t->headers, max, remaining, t->cols, f);

        fputc(INTERSECT, f);
        fputnc('-', width - 2, f);
        fputc(INTERSECT, f);
        fputc('\n', f);

        for (i = 0; i < t->rows; ++i) {
                print_row(t->data[i], max, remaining, t->cols, f);

                if (i + 1 < t->rows) {
                        size_t j;
                        fputc('|', f);
                        for (j = 0; j < t->cols; ++j) {
                                fputnc('-', max[j] + 2, f);
                                fputc('|', f);
                        }
                        fputc('\n', f);
                }
        }

        fputc(CORNER, f);
        fputnc('-', width - 2, f);
        fputc(CORNER, f);
        fputc('\n', f);

        free(max);
        free(remaining);

        return true;
}

void table_free(struct table *t)
{
        size_t i, j;

        for (i = 0; i < t->rows; ++i) {
                for (j = 0; j < t->cols; ++j) {
                        free(t->data[i][j]);
                }
        }

        for (i = 0; i < t->rows; ++i) {
                free(t->data[i]);
        }

        free(t->data);
        free(t->headers);
        free(t->max);
        free(t->fmt);
}
