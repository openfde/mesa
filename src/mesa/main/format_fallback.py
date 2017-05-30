# Copyright 2017 Google
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the
# "Software"), to deal in the Software without restriction, including
# without limitation the rights to use, copy, modify, merge, publish,
# distribute, sub license, and/or sell copies of the Software, and to
# permit persons to whom the Software is furnished to do so, subject to
# the following conditions:
#
# The above copyright notice and this permission notice (including the
# next paragraph) shall be included in all copies or substantial portions
# of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
# OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
# MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
# IN NO EVENT SHALL VMWARE AND/OR ITS SUPPLIERS BE LIABLE FOR
# ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
# TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
# SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

# stdlib
import argparse
from sys import stdout
from textwrap import dedent

# local
import format_parser

def format_is_rgbx(fmt):
    return fmt.has_channel('a')

def parse_args():
    p = argparse.ArgumentParser()
    p.add_argument("csv")
    return p.parse_args()

def write_preamble(out):
    out.write(dedent('''\
        /*
         * Copyright 2017 Google
         *
         * Permission is hereby granted, free of charge, to any person obtaining a
         * copy of this software and associated documentation files (the "Software"),
         * to deal in the Software without restriction, including without limitation
         * the rights to use, copy, modify, merge, publish, distribute, sublicense,
         * and/or sell copies of the Software, and to permit persons to whom the
         * Software is furnished to do so, subject to the following conditions:
         *
         * The above copyright notice and this permission notice shall be included
         * in all copies or substantial portions of the Software.
         *
         * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
         * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
         * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
         * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
         * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
         * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
         * OTHER DEALINGS IN THE SOFTWARE.
         */

         /*
          * This file is GENERATED.  Do not edit it manually or commit it into version
          * control.
          */

          #include "format_fallback.h"

          '''))

def format_get_alpha_channel_string(fmt):
    """Return alpha channel's substring in the format name. If the format has
    no alpha channel, then return None.

    The name of alpha formats fall into the following cases:

       - The alpha channel's size follows the "A". For example,
         MESA_FORMAT_R8G8B8A8_UNORM. In this case, the channel substring
         is "A8".

       - The alpha channel's size implicitly appears as the datatype size. For
         example, MESA_FORMAT_RGBA_FLOAT32. In this case, the channel
         substring is just "A".
    """

    alpha_channel = fmt.get_channel('a')
    if not alpha_channel:
        return None

    assert alpha_channel.size > 0

    alpha_channel_name = "A" + str(alpha_channel.size)
    if alpha_channel_name in fmt.name:
        return alpha_channel_name

    return "A"

def format_convert_alpha_to_x(fmt, all_formats):
    """If the format is an alpha format for which a non-alpha variant
    exists with the same bit layout, then return the non-alpha format.
    Otherwise return None.
    """

    a_str = format_get_alpha_channel_string(fmt)
    if not a_str:
        # Format has no alpha channel
        return None

    x_str = a_str.replace("A", "X")

    # Succesively replace each occurence of a_str in the format name with
    # x_str until we find a valid format name.
    i = -1
    while True:
        i += 1
        i = fmt.name.find(a_str, i)
        if i == -1:
            break

        x_fmt_name = fmt.name[:i] + fmt.name[i:].replace(a_str, x_str)
        # Assert that the string replacement actually occured.
        assert x_fmt_name != fmt.name

        x_fmt = all_formats.get(x_fmt_name, None)
        if x_fmt is None:
            continue

        return x_fmt

    return None

def write_func_mesa_format_fallback_rgbx_to_rgba(out, formats):
    out.write(dedent('''\
        /**
          * If the format has an alpha channel, and there exists a non-alpha
          * variant of the format with an identical bit layout, then return
          * the non-alpha format. Otherwise return the original format.
          *
          * Examples:
          *    Fallback exists:
          *       MESA_FORMAT_R8G8B8X8_UNORM -> MESA_FORMAT_R8G8B8A8_UNORM
          *       MESA_FORMAT_RGBX_UNORM16 -> MESA_FORMAT_RGBA_UNORM16
          *
          *    No fallback:
          *       MESA_FORMAT_R8G8B8A8_UNORM -> MESA_FORMAT_R8G8B8A8_UNORM
          *       MESA_FORMAT_Z_FLOAT32 -> MESA_FORMAT_Z_FLOAT32
          */
        mesa_format
        _mesa_format_fallback_rgbx_to_rgba(mesa_format format)
        {
           switch (format) {
        '''))

    for alpha_format in formats.values():
        x_format = format_convert_alpha_to_x(alpha_format, formats)
        if x_format is None:
            continue

        out.write("   case {}: return {};\n".format(
            x_format.name, alpha_format.name))

    out.write(dedent('''\
           default: return format;
           }
        }
        '''))

def main():
    pargs = parse_args()

    formats = {}
    for fmt in format_parser.parse(pargs.csv):
        formats[fmt.name] = fmt

    write_preamble(stdout)
    write_func_mesa_format_fallback_rgbx_to_rgba(stdout, formats)

if __name__ == "__main__":
    main()
