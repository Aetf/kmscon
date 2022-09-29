# SPDX-FileCopyrightText: 2022 Aetf <aetf@unlimited-code.works>
#
# SPDX-License-Identifier: MIT

manpages = [
  'kmscon.1.xml.in',
]

data = configuration_data()
data.set('CONFIG_DIR', prefix / sysconfdir)

foreach man : manpages
  xml = configure_file(input: f'man/@man@', output: '@BASENAME@', configuration: data)
  name = fs.replace_suffix(fs.replace_suffix(man, ''), '')
  custom_target(
    name,
    command: [
      xsltproc,
      '--stringparam', 'man.authors.section.enabled', '0',
      '--stringparam', 'man.copyright.section.enabled', '0',
      '--stringparam', 'funcsynopsis.style', 'ansi',
      '--stringparam', 'man.output.quietly', '1',
      '--nonet',
      '-o', '@OUTPUT@',
      manpages_stylesheet,
      '@INPUT@'
    ],
    input: [
      xml
    ],
    output: name,
    build_by_default: true,
    install: true,
    install_dir: mandir / 'man1',
  )
endforeach

