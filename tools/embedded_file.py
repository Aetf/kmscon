#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2022 Aetf <aetf@unlimited-code.works>
#
# SPDX-License-Identifier: MIT
import inspect
import re
from pathlib import Path
from typing import List, Tuple
from itertools import islice


def chunk(it, size):
    it = iter(it)
    return iter(lambda: tuple(islice(it, size)), ())


def make_c_identifier(s):
    for ptn, repl in [
        (re.compile(r'[^a-zA-Z0-9_]'), '_'),
        (re.compile(r'^(\d)'), r'_\1'),
    ]:
        s = re.sub(ptn, repl, s)
    return s


def generate(
    src: Path,
    dest_dir: Path,
    regex: List[Tuple[re.Pattern, str]],
    zero_term: bool,
):
    # prepare
    name = src.name
    c_name = make_c_identifier(name)

    # read content
    if regex:
        with src.open() as f:
            lines = []
            for line in f:
                for ptn, sub in regex:
                    line = ptn.sub(sub, line)
                lines.append(line)
            content = '\n'.join(lines).encode('utf-8')
    else:
        with src.open('rb') as f:
            content = f.read()

    if zero_term:
        content = content + b'\x00'

    # format content to c array and 16 bytes per row
    content_output = ',\n            '.join(
        ', '.join(f'{b:#04x}' for b in row)
        for row in chunk(content, 16)
    )

    content_c = inspect.cleandoc(f'''
        #include <stddef.h>
        static const unsigned char data[] = {{
            {content_output}
        }};
        const char *const _binary_{c_name}_start = (const char*)data;
        const char *const _binary_{c_name}_end = (const char*)data + sizeof(data);
        const size_t _binary_{c_name}_size = sizeof(data);
    ''')
    content_h = inspect.cleandoc(f'''
        #ifndef {c_name}_H
        #define {c_name}_H
        #include <stddef.h>
        extern const char *const _binary_{c_name}_start;
        extern const char *const _binary_{c_name}_end;
        extern size_t _binary_{c_name}_size;
        #endif // {c_name}_H
    ''')

    # generate the file
    with (dest_dir / f'{name}.bin.c').open('w') as f:
        f.write(content_c)
    with (dest_dir / f'{name}.bin.h').open('w') as f:
        f.write(content_h)
    dest_dir.mkdir(exist_ok=True, parents=True)


def main():
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument(
        '--zero_term',
        action='store_true',
        help='Add an additional 0 byte at the end of data'
    )
    parser.add_argument(
        '--regex', metavar=('pattern', 'replace'),
        type=str, nargs = 2, action='append', default=[],
        help='Treat the binary as text, and for each line replace all occurance matching the regex'
    )
    parser.add_argument(
        'src',
        type=Path,
        help='The source binary file'
    )
    parser.add_argument(
        'dest_dir',
        type=Path,
        help='The destination directory to put output files'
    )
    args = parser.parse_args()

    args.regex = [
        (re.compile(ptn), sub)
        for ptn, sub in args.regex
    ]
    generate(args.src, args.dest_dir, args.regex, args.zero_term)


if __name__ == '__main__':
    main()
