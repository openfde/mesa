#encoding=utf-8

from __future__ import (
    absolute_import, division, print_function, unicode_literals
)

import argparse
import io
import os
import os.path
import re
import sys
from sys import stdout, stderr
from textwrap import dedent
import xml.parsers.expat

def safe_token(t):
    t = t.replace(' ', '')
    if t[0].isdigit():
        t = '_' + t
    return t

class Gen(object):

    def __init__(self, z):
        # Convert potential "major.minor" string
        z = float(z)
        if z < 10:
            z *= 10
        self._10x = int(z)

    def prefix(self, token):
        gen = self._10x

        if gen % 10 == 0:
            gen //= 10

        if token[0] == '_':
            token = token[1:]

        return 'GEN{}_{}'.format(gen, token)

class Header(object):

    def __init__(self, buf):
        self.buf = buf
        self.cpp_guard = os.path.basename(buf.name).upper().replace('.', '_')

    def write(self, *args, **kwargs):
        self.buf.write(*args, **kwargs)

    def write_prologue(self):
        self.write(dedent("""\
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

             """))

        self.write('#ifndef {}\n'.format(self.cpp_guard))
        self.write('#define {}\n'.format(self.cpp_guard))

        self.write(dedent("""
            #include <stdint.h>

            #ifdef __cplusplus
            extern "C" {
            #endif

            """))

    def write_epilogue(self):
        self.write(dedent("""\
            #ifdef __cplusplus
            }
            #endif

            """))

        self.write('#endif /* {} */\n'.format(self.cpp_guard))

    def write_macros(self, fields):
        for gen_10x in sorted(fields.by_gen_10x.keys(), reverse=True):
            for f in fields.by_gen_10x[gen_10x]:
                self.write('#define {:56} {:2}'.format(f.token_name, f.bits))
                if f.comment:
                    self.write(' /* {} */'.format(f.comment))
                self.write('\n')
            self.write('\n')

    def write_funcs(self, fields):
        def gen_10x(field):
            return field.gen._10x

        for basename in sorted(fields.by_token_basenames.keys()):
            self.write('static inline uint32_t __attribute__((const))\n')
            self.write('{}(int gen_10x)\n'.format(basename))
            self.write('{\n')
            self.write('   switch (gen_10x) {\n')

            for f in sorted(fields.by_token_basenames[basename],
                            key=gen_10x, reverse=True):
                self.write('   case {}: return {};\n'
                           .format(f.gen._10x, f.token_name))

            self.write('   default: return 0;\n')
            self.write('   }\n')
            self.write('}\n')
            self.write('\n')

class Field(object):

    SURFACE_PITCH_REGEX = re.compile(r'.*Surface (Q?)Pitch$')

    def __init__(self, gen, container_name, xml_attrs, comment=None):
        assert isinstance(gen, Gen)
        assert container_name

        self.gen = gen
        self.container_name = container_name
        self.name = xml_attrs['name']
        self.start = int(xml_attrs['start'])
        self.end = int(xml_attrs['end'])
        self.bits = 1 + self.end - self.start
        self.token_basename = '_'.join([safe_token(container_name), safe_token(self.name), 'bits'])
        self.token_name = gen.prefix(self.token_basename)
        self.comment = comment

class FieldCollection(object):

    def __init__(self):
        self.by_gen_10x = {}
        self.by_token_basenames = {}

    def add(self, field):
        self.by_gen_10x.setdefault(field.gen._10x, []).append(field)
        self.by_token_basenames.setdefault(field.token_basename, []).append(field)

class XmlParser(object):

    def __init__(self, field_collection):
        self.parser = xml.parsers.expat.ParserCreate()
        self.parser.StartElementHandler = self.start_element
        self.parser.EndElementHandler = self.end_element

        self.fields = field_collection
        self.container_name = None

    def parse(self, filename):
        with open(filename) as f:
            self.parser.ParseFile(f)

    def start_element(self, name, attrs):
        if name == 'genxml':
            self.gen = Gen(attrs['gen'])
        elif name in ('instruction', 'struct', 'register'):
            self.start_container(attrs)
        elif name == 'field':
            self.start_field(attrs)
        else:
            pass

    def end_element(self, name):
        if name == 'genxml':
            self.gen = None
        elif name in ('instruction', 'struct', 'register'):
            self.container_name = None
        else:
            pass

    def start_container(self, attrs):
        assert self.container_name is None
        name = attrs['name']

        # We don't care about these
        if re.search(r'STREAMOUT|3DSTATE_SO', name):
            return

        self.container_name = safe_token(name)

    def start_field(self, attrs):
        if self.container_name is None:
            return

        name = attrs.get('name', None)
        if not name:
            return

        match = Field.SURFACE_PITCH_REGEX.match(name)
        if not match:
            return

        self.fields.add(Field(self.gen, self.container_name, attrs))

        if name == 'MCS Surface Pitch':
            # MCSSurfacePitch in older gens is analogous to
            # AuxiliarySurfacePitch in newer gens.
            aux_attrs = attrs.copy()
            aux_attrs['name'] = 'Auxiliary Surface Pitch'
            self.fields.add(Field(self.gen, self.container_name, aux_attrs,
                                  comment='alias of MCSSurfacePitch'))

def parse_args():
    p = argparse.ArgumentParser()
    p.add_argument('-o', '--output', type=str)
    p.add_argument('sources', metavar='SOURCES', nargs=argparse.REMAINDER)

    pargs = p.parse_args()

    if len(pargs.sources) == 0:
        stderr.write('error: no source files\n')
        sys.exit(1)

    if pargs.output in (None, '-'):
        pargs.output = '/dev/stdout'

    return pargs

def main():
    pargs = parse_args()

    fields = FieldCollection()

    for source in pargs.sources:
        XmlParser(fields).parse(source)

    with open(pargs.output, 'w') as outfile:
        header = Header(outfile)
        header.write_prologue()
        header.write_macros(fields)
        header.write_funcs(fields)
        header.write_epilogue()

if __name__ == '__main__':
    main()
