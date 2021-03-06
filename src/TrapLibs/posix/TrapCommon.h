/*
 * This file is part of selfrando.
 * Copyright (c) 2017 Immunant Inc.
 * For license information, see the LICENSE file
 * included with selfrando.
 *
 */

#pragma once

#define DEFINE_SECTION_SYMBOL(symbol, section_name, ...)    \
    .section section_name, ##__VA_ARGS__;                   \
    .globl symbol;                                          \
    .hidden symbol;                                         \
    .type symbol, %object;                                  \
    symbol:
