{
  uses_user_defaults: true,
  security_policy_supported_versions: { '0.0.x': ':white_check_mark:' },
  project_name: 'bfcodec',
  version: '0.0.5',
  description: 'Tools and a C/C++ library to manipulate BFCodec-encrypted content.',
  social+: {
    custom_badges: [
      '[![Tests](https://github.com/Tatsh/bfcodec/actions/workflows/tests.yml/badge.svg)](https://github.com/Tatsh/bfcodec/actions/workflows/tests.yml)',
      '[![Coverage Status](https://coveralls.io/repos/github/Tatsh/bfcodec/badge.svg?branch=master)](https://coveralls.io/github/Tatsh/bfcodec?branch=master)',
    ],
  },
  keywords: ['android', 'ios', 'konami', 'jubeat', 'jukebeat', 'reflec beat'],
  want_codeql: false,
  want_main: false,
  want_tests: false,
  clang_format_args: 'include/*.h src/*.c* tools/*.c* tools/*.h',
  package_json+: {
    cspell+: {
      ignorePaths+: [
        '*.patch',
        '.docs/*.tag.xml',
        '.docs/*.tags',
      ],
    },
  },
  prettierignore+: ['*.cc', '*.inc', '*.patch', '*.tags'],
  cz+: {
    commitizen+: {
      version_files+: [
        'man/jbt.1',
        'man/unjbt.1',
      ],
    },
  },
  vcpkg+: {
    dependencies+: ['argparse', 'libplist', 'libzip', 'spdlog'],
  },
  vscode+: {
    c_cpp+: {
      configurations: [
        {
          cStandard: 'gnu23',
          compilerPath: '/usr/bin/gcc',
          cppStandard: 'gnu++23',
          includePath: [
            '${workspaceFolder}/include/**',
            '${workspaceFolder}/src/**',
            '${workspaceFolder}/tools/**',
          ],
          name: 'Linux',
        },
      ],
    },
  },
  project_type: 'c++',
}
