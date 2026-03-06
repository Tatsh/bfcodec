{
  security_policy_supported_versions: { '0.0.x': ':white_check_mark:' },
  project_name: 'bfcodec',
  version: '0.0.0',
  description: 'C/C++ library to decrypt BFCodec-encrypted content.',
  keywords: ['ios', 'konami', 'jubeat', 'jukebeat', 'reflec beat'],
  want_main: false,
  copilot+: {
    intro: 'BFCodec is a C/C++ library to decrypt BFCodec-encrypted content as seen in iOS BEMANI games.',
  },
  package_json+: {
    cspell+: {
      ignorePaths+: [
        '.docs/*.tags',
        '.docs/*.tag.xml',
      ],
    },
    local clang_format_globs = 'include/*.h src/*.c*',
    scripts+: {
      'check-formatting': 'clang-format -n %s && prettier -c . && markdownlint-cli2' % clang_format_globs,
      format: 'clang-format -i %s && yarn prettier -w .' % clang_format_globs,
    },
  },
  prettierignore+: ['*.hpp', '*.inc', '*.tags'],
  cz+: {
    commitizen+: {
      version_files+: [
        'CMakeLists.txt',
        'man/bfcodec.1',
        'src/main.cpp',
      ],
    },
  },
  vscode+: {
    c_cpp+: {
      configurations: [
        {
          cStandard: 'c23',
          compilerPath: '/usr/bin/gcc',
          cppStandard: 'c++23',
          includePath: [
            '${workspaceFolder}/include/**',
            '${workspaceFolder}/src/**',
          ],
          name: 'Linux',
        },
      ],
    },
  },
  project_type: 'c++',
}
