/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Base64 encoding implementation
 * Copyright (C) 2025 Vo Minh Duc
 */

#include <stddef.h>
#include <stdlib.h>
#include <stdint.h>

#include "base64.h"

const unsigned char b64_table[] ="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/**
 * @brief Encode data to Base64 string.
 *
 * @param data          Input data.
 * @param input_length  Number of bytes in the input buffer.
 * @param output_length A size_t pointer to receive the length of the encoded string (without the terminating '\0'). Pass NULL if not needed.
 *
 * @return Newly allocated null-terminated Base64 string on success, or NULL on failure. Caller must free().
 */
unsigned char* base64_encode(const unsigned char *data, size_t input_length, size_t *output_length) {
    size_t                  out_len = 4 * ((input_length + 2) / 3);
    unsigned char           *encoded_data = NULL;
    size_t                  i = 0, out = 0, remaining;
    uint32_t                octet_1, octet_2, octet_3, triple;


    encoded_data = malloc(out_len + 1);
    if (encoded_data == NULL) return NULL;

    for (i = 0, out = 0; i < input_length;) {
        remaining = input_length - i;

        octet_1 = i < input_length ? data[i++] : 0;
        octet_2 = (remaining > 1) ? data[i++] : 0;
        octet_3 = (remaining > 2) ? data[i++] : 0;

        triple = (octet_1 << 16) | (octet_2 << 8) | (octet_3 << 0);

        encoded_data[out++] = b64_table[(triple >> 18) & 63];
        encoded_data[out++] = b64_table[(triple >> 12) & 63];
        encoded_data[out++] = (remaining > 1) ? b64_table[(triple >> 6) & 63] : '=';
        encoded_data[out++] = (remaining > 2) ? b64_table[(triple >> 0) & 63] : '=';
    }

    encoded_data[out_len] = '\0';
    if (output_length != NULL) {
        *output_length = out_len;
    }
    return encoded_data;
}