/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Base64 encoding implementation
 * Copyright (C) 2025 Vo Minh Duc
 */

#ifndef BASE64_H
#define BASE64_H

unsigned char* base64_encode(const unsigned char *data, size_t input_length, size_t *output_length);

#endif