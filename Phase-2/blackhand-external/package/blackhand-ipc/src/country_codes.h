/*
 * country_codes.h — static country dial-code table + E.164 normalisation.
 *
 * Self-contained module: no dependencies beyond libc. Both the UI (contact
 * entry, dial pad) and any service can link it via libblackhand-ipc.
 *
 * The rule, per country:
 *   1. strip separators (spaces, dashes, parens, dots) from user input
 *   2. if it already starts with '+'  → already E.164, pass through
 *   3. if it starts with "00"         → replace with '+', pass through
 *   4. otherwise: strip the country's trunk prefix (if any), prepend the
 *      country dial code
 *
 *   Australia: "0413 805 252" + AU(+61, trunk "0") → "+61413805252"
 *   UK:        "07700900001"  + GB(+44, trunk "0") → "+447700900001"
 *   USA:       "2025551234"   + US(+1,  trunk "")  → "+12025551234"
 *   Italy:     "0636001"      + IT(+39, trunk "")  → "+390636001"
 */
#ifndef COUNTRY_CODES_H
#define COUNTRY_CODES_H

#include <stddef.h>

typedef struct {
    const char *name;   /* "Australia"          */
    const char *iso;    /* "AU" (settings key)  */
    const char *code;   /* "+61"                */
    const char *trunk;  /* "0", "8", or ""      */
} CountryCode;

/* The full table, terminated by a {NULL,...} entry. *count (optional) gets
 * the number of real entries. */
const CountryCode *country_table(size_t *count);

/* Find by ISO code ("AU") — returns NULL if unknown. */
const CountryCode *country_by_iso(const char *iso);

/* Normalise raw user input to E.164 using the given country (may be NULL —
 * then only rules 1-3 apply and national numbers are left bare).
 * Returns 0 on success, -1 if input has no digits or output won't fit.
 * out is always NUL-terminated on success, e.g. "+61413805252". */
int phone_normalize(const char *raw, const CountryCode *cc,
                    char *out, size_t out_sz);

/* 1 if s looks like an E.164 number: '+' followed by 7-15 digits. */
int phone_is_e164(const char *s);

#endif
