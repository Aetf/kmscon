#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2022 Aetf <aetf@unlimited-code.works>
#
# SPDX-License-Identifier: MIT
import re
import inspect

PTN_RELEASE_NOTE = re.compile(r'^CHANGES WITH.+$([\s\S]+?)^CHANGES WITH.+$', re.MULTILINE)


def extract(src: str) -> str:
    """Find section between the first and the next 'CHANGES WITH xx:' lines,
    and strip common prefix whitespace
    """
    m = PTN_RELEASE_NOTE.search(src)
    if m is not None:
        return inspect.cleandoc(m.group(1)) + '\n'
    else:
        return ''


def main():
    import argparse
    import sys

    parser = argparse.ArgumentParser()
    parser.add_argument(
        'src',
        type=argparse.FileType('r'),
        nargs='?',
        help='input NEWS',
        default=sys.stdin,
    )
    parser.add_argument(
        'dest',
        type=argparse.FileType('w'),
        nargs='?',
        help='output',
        default=sys.stdout,
    )
    args = parser.parse_args()

    args.dest.write(extract(args.src.read()))


if __name__ == '__main__':
    main()
