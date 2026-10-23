#!/usr/bin/env python3
# Copyright 2026 Dao Browser Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Embeds a UTF-8 text file into a C++ header as a raw string constant."""

import os
import sys

DELIMITER = 'DAOTEXT'


def main(argv):
    if len(argv) != 4:
        print('usage: embed_text.py SOURCE OUTPUT NAME', file=sys.stderr)
        return 2
    source, output, name = argv[1:]
    with open(source, encoding='utf-8') as f:
        text = f.read()
    if ')' + DELIMITER + '"' in text:
        print(f'{source} contains the raw string delimiter', file=sys.stderr)
        return 1
    guard = name.upper() + '_H_'
    os.makedirs(os.path.dirname(output) or '.', exist_ok=True)
    with open(output, 'w', encoding='utf-8') as f:
        f.write(f'// Generated from {os.path.basename(source)} by '
                'embed_text.py. Do not edit.\n\n')
        f.write(f'#ifndef {guard}\n#define {guard}\n\n')
        f.write('namespace dao {\n\n')
        f.write(f'inline constexpr char {name}[] = R"{DELIMITER}(')
        f.write(text)
        f.write(f'){DELIMITER}";\n\n')
        f.write('}  // namespace dao\n\n')
        f.write(f'#endif  // {guard}\n')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
