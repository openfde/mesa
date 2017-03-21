#encoding=utf-8

from __future__ import (
    absolute_import, division, print_function, unicode_literals
)

import io
import os
import os.path
import re
import sys
from sys import stdout
from textwrap import dedent

prologue = dedent("""\
    /*
     * Copyright (C) 2017 Intel Corporation
     *
     * Permission is hereby granted, free of charge, to any person obtaining a
     * copy of this software and associated documentation files (the "Software"),
     * to deal in the Software without restriction, including without limitation
     * the rights to use, copy, modify, merge, publish, distribute, sublicense,
     * and/or sell copies of the Software, and to permit persons to whom the
     * Software is furnished to do so, subject to the following conditions:
     *
     * The above copyright notice and this permission notice (including the next
     * paragraph) shall be included in all copies or substantial portions of the
     * Software.
     *
     * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
     * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
     * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
     * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
     * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
     * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
     * IN THE SOFTWARE.
     */

    /* THIS FILE HAS BEEN GENERATED, DO NOT HAND EDIT.
     *
     * Sizes of bitfields in genxml instructions, structures, and registers.
     */

    #ifndef GENX_BITS_H
    #define GENX_BITS_H

    #include <stdint.h>

    #ifdef __cplusplus
    extern "C" {
    #endif

    """)

epilogue = dedent("""\
    #ifdef __cplusplus
    }
    #endif

    #endif /* GENX_BITS_H */
    """)

def sanitize_token(t):
    if t[0].isdigit():
        return '_' + t
    else:
        return t

def genx_prefix(x):
    return 'GENX_' + x

class BitsLine:

    __slots__ = (
        'orig_line',
        'gen_10x',
        'token_name',
        'token_basename',
    )

    REGEX = re.compile('^#define (?P<token_name>GEN(?P<gen>[0-9]+)_(?P<token_basename>\w*Surface(Q?)Pitch)_bits).*$')

    def __init__(self, match):
        self.orig_line = match.group(0)

        self.gen_10x = int(match.group('gen'))
        if self.gen_10x < 10:
            self.gen_10x *= 10

        self.token_name = match.group('token_name')
        self.token_basename = match.group('token_basename')

        # MCSSurfacePitch in older gens is analogous to AuxiliarySurfacePitch
        # in newer gens.
        self.token_basename = \
            self.token_basename.replace('MCSSurfacePitch', 'AuxiliarySurfacePitch')

class BitsCollection:

    def __init__(self):
        self.by_gen_10x = {}
        self.by_token_basenames = {}

    def add(self, bits_line):
        # We don't care about 3DSTATE_STREAMOUT.
        if ('STREAMOUT' in bits_line.token_name or
            'SO_BUFFER' in bits_line.token_name):
            return

        self.by_gen_10x.setdefault(bits_line.gen_10x, []).append(bits_line)
        self.by_token_basenames.setdefault(bits_line.token_basename, []).append(bits_line)

    def read_filepath(self, path):
        with open(path) as file:
            for line in file:
                m = BitsLine.REGEX.match(line)
                if not m:
                    continue
                self.add(BitsLine(m))

    def write_macros(self, out):
        for gen_10x in sorted(self.by_gen_10x.keys(), reverse=True):
            for bits_line in self.by_gen_10x[gen_10x]:
                out.write(bits_line.orig_line)
                out.write('\n')
            out.write('\n')

    def write_funcs(self, out):
        for token_basename in sorted(self.by_token_basenames.keys()):
            out.write('static inline uint32_t __attribute__((const))\n')
            out.write('{}_bits(int gen_10x)\n'.format(sanitize_token(token_basename)))
            out.write('{\n')
            out.write('   switch (gen_10x) {\n')

            def sort_key(bits_line):
                return bits_line.gen_10x

            for bits_line in sorted(self.by_token_basenames[token_basename],
                                    key=sort_key, reverse=True):
                out.write('   case {}: return {};\n'.format(bits_line.gen_10x, bits_line.token_name))

            out.write('   default: return 0;\n')
            out.write('   }\n')
            out.write('}\n')
            out.write('\n')

def main():
    sources = sorted(sys.argv[1:])
    if len(sources) == 0:
        sys.stderr.write('error: no source files\n')
        sys.exit(1)

    bits_collection = BitsCollection()

    for path in sources:
        bits_collection.read_filepath(path)

    sys.stdout.write(prologue)
    bits_collection.write_macros(stdout)
    bits_collection.write_funcs(stdout)
    sys.stdout.write(epilogue)

if __name__ == '__main__':
    main()
