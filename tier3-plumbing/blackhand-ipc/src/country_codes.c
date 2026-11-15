/*
 * country_codes.c — static dial-code table + E.164 normalisation.
 * See country_codes.h for the rules.
 */
#include "country_codes.h"
#include <string.h>
#include <ctype.h>

/* name, iso, dial code, trunk prefix ("" = none).
 * Trunk prefix is what the user dials domestically before the subscriber
 * number and must be stripped when converting to E.164. Most countries use
 * "0"; NANP (+1) and Italy use none; Russia/Kazakhstan use "8". */
static const CountryCode TABLE[] = {
    { "Afghanistan",    "AF", "+93",  "0" },
    { "Australia",      "AU", "+61",  "0" },
    { "Austria",        "AT", "+43",  "0" },
    { "Bangladesh",     "BD", "+880", "0" },
    { "Belgium",        "BE", "+32",  "0" },
    { "Brazil",         "BR", "+55",  "0" },
    { "Canada",         "CA", "+1",   ""  },
    { "China",          "CN", "+86",  "0" },
    { "Denmark",        "DK", "+45",  ""  },
    { "Egypt",          "EG", "+20",  "0" },
    { "France",         "FR", "+33",  "0" },
    { "Germany",        "DE", "+49",  "0" },
    { "Greece",         "GR", "+30",  ""  },
    { "India",          "IN", "+91",  "0" },
    { "Indonesia",      "ID", "+62",  "0" },
    { "Iran",           "IR", "+98",  "0" },
    { "Iraq",           "IQ", "+964", "0" },
    { "Ireland",        "IE", "+353", "0" },
    { "Italy",          "IT", "+39",  ""  },
    { "Japan",          "JP", "+81",  "0" },
    { "Mexico",         "MX", "+52",  ""  },
    { "Netherlands",    "NL", "+31",  "0" },
    { "New Zealand",    "NZ", "+64",  "0" },
    { "Nigeria",        "NG", "+234", "0" },
    { "Norway",         "NO", "+47",  ""  },
    { "Pakistan",       "PK", "+92",  "0" },
    { "Philippines",    "PH", "+63",  "0" },
    { "Poland",         "PL", "+48",  ""  },
    { "Portugal",       "PT", "+351", ""  },
    { "Russia",         "RU", "+7",   "8" },
    { "Saudi Arabia",   "SA", "+966", "0" },
    { "South Africa",   "ZA", "+27",  "0" },
    { "South Korea",    "KR", "+82",  "0" },
    { "Spain",          "ES", "+34",  ""  },
    { "Sweden",         "SE", "+46",  "0" },
    { "Switzerland",    "CH", "+41",  "0" },
    { "Turkey",         "TR", "+90",  "0" },
    { "UAE",            "AE", "+971", "0" },
    { "United Kingdom", "GB", "+44",  "0" },
    { "United States",  "US", "+1",   ""  },
    { NULL, NULL, NULL, NULL }
};

const CountryCode *country_table(size_t *count)
{
    if (count) {
        size_t n = 0;
        while (TABLE[n].name) n++;
        *count = n;
    }
    return TABLE;
}

const CountryCode *country_by_iso(const char *iso)
{
    if (!iso) return NULL;
    for (size_t i = 0; TABLE[i].name; i++)
        if (strcmp(TABLE[i].iso, iso) == 0)
            return &TABLE[i];
    return NULL;
}

/* Copy only '+' (first char) and digits into dst. Returns digit count. */
static size_t strip_separators(const char *src, char *dst, size_t dst_sz)
{
    size_t w = 0, digits = 0;
    for (size_t r = 0; src[r] && w + 1 < dst_sz; r++) {
        char c = src[r];
        if (c == '+' && w == 0) {
            dst[w++] = c;
        } else if (isdigit((unsigned char)c)) {
            dst[w++] = c;
            digits++;
        }
        /* spaces, dashes, parens, dots: skipped */
    }
    dst[w] = '\0';
    return digits;
}

int phone_is_e164(const char *s)
{
    if (!s || s[0] != '+') return 0;
    size_t n = strlen(s + 1);
    if (n < 7 || n > 15) return 0;
    for (size_t i = 1; s[i]; i++)
        if (!isdigit((unsigned char)s[i])) return 0;
    return 1;
}

int phone_normalize(const char *raw, const CountryCode *cc,
                    char *out, size_t out_sz)
{
    if (!raw || !out || out_sz < 4) return -1;

    char clean[64];
    if (strip_separators(raw, clean, sizeof(clean)) == 0) return -1;

    /* Already international? */
    if (clean[0] == '+') {
        if (strlen(clean) >= out_sz) return -1;
        strcpy(out, clean);
        return 0;
    }
    if (clean[0] == '0' && clean[1] == '0') {
        /* "00" international prefix → "+" */
        if (strlen(clean) - 1 >= out_sz) return -1;
        out[0] = '+';
        strcpy(out + 1, clean + 2);
        return 0;
    }

    /* National number. Without a country we can't do better than digits. */
    if (!cc) {
        if (strlen(clean) >= out_sz) return -1;
        strcpy(out, clean);
        return 0;
    }

    const char *num = clean;
    size_t tlen = strlen(cc->trunk);
    if (tlen && strncmp(num, cc->trunk, tlen) == 0)
        num += tlen;                      /* strip trunk prefix */

    if (strlen(cc->code) + strlen(num) >= out_sz) return -1;
    strcpy(out, cc->code);
    strcat(out, num);
    return 0;
}
