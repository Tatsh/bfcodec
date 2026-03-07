{
  security_policy_supported_versions: { '0.0.x': ':white_check_mark:' },
  project_name: 'bfcodec',
  version: '0.0.1',
  description: 'Tools and a C/C++ library to manipulate BFCodec-encrypted content.',
  keywords: ['android', 'ios', 'konami', 'jubeat', 'jukebeat', 'reflec beat'],
  want_codeql: false,
  want_main: false,
  want_tests: false,
  copilot+: {
    intro: 'BFCodec is a set of tools and a C/C++ library to decrypt BFCodec-encrypted content as seen in Android/iOS BEMANI games.',
  },
  package_json+: {
    cspell+: {
      ignorePaths+: [
        '*.patch',
        '.docs/*.tag.xml',
        '.docs/*.tags',
      ],
    },
    local clang_format_globs = 'include/*.h src/*.c* tools/*.c* tools/*.h',
    scripts+: {
      'check-formatting': 'clang-format -n %s && prettier -c . && markdownlint-cli2' % clang_format_globs,
      format: 'clang-format -i %s && yarn prettier -w .' % clang_format_globs,
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
