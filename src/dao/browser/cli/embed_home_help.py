#!/usr/bin/env python3
# Copyright 2026 Dao Browser Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Embed offline CLI help from the same GRIT English/Chinese browser resources."""

import json
from pathlib import Path
import sys
import xml.etree.ElementTree as ET


def main(argv):
    if len(argv) != 5:
        raise ValueError('expected GRIT_ROOT GRD XTB OUTPUT')
    sys.path.insert(0, argv[1])
    from grit.extern.tclib import GenerateMessageId
    message = ET.parse(argv[2]).find('.//message[@name="IDS_DAO_HOME_CLI_HELP"]')
    if message is None or len(message):
        raise ValueError('help must be plain localized text')
    english = (message.text or '').strip()
    translated = ET.parse(argv[3]).find(
        f'.//translation[@id="{GenerateMessageId(english)}"]')
    if translated is None:
        raise ValueError('missing hand-authored Chinese help')
    chinese = ''.join(translated.itertext()).strip()
    output = Path(argv[4])
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(
        '// Generated from Dao GRIT resources. Do not edit.\n'
        '#ifndef DAO_BROWSER_CLI_DAO_HOME_HELP_H_\n'
        '#define DAO_BROWSER_CLI_DAO_HOME_HELP_H_\n'
        'namespace dao {\n'
        f'inline constexpr char kDaoHomeHelpEn[] = {json.dumps(english, ensure_ascii=False)};\n'
        f'inline constexpr char kDaoHomeHelpZh[] = {json.dumps(chinese, ensure_ascii=False)};\n'
        '}  // namespace dao\n'
        '#endif  // DAO_BROWSER_CLI_DAO_HOME_HELP_H_\n', encoding='utf-8')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
