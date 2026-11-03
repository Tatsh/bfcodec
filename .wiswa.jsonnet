{
  uses_user_defaults: true,
  security_policy_supported_versions: { '0.0.x': ':white_check_mark:' },
  project_name: 'bfcodec',
  version: '0.0.6',
  description: 'Tools and a C/C++ library to manipulate BFCodec-encrypted content.',
  custom_project_badges: [
    {
      anchor: '[![Tests](https://github.com/Tatsh/bfcodec/actions/workflows/tests.yml/badge.svg)]',
      href: 'https://github.com/Tatsh/bfcodec/actions/workflows/tests.yml',
    },
    {
      anchor: '[![Coverage Status](https://coveralls.io/repos/github/Tatsh/bfcodec/badge.svg?branch=master)]',
      href: 'https://coveralls.io/github/Tatsh/bfcodec?branch=master',
    },
  ],
  keywords: ['android', 'ios', 'konami', 'jubeat', 'jukebeat', 'reflec beat'],
  want_codeql: false,
  want_main: false,
  want_tests: false,
  clang_format_args: 'include/*.h src/*.c* tools/*.c* tools/*.h',
  shared_ignore+: ['.deploy-rb-*', 'mulist*'],
  package_json+: {
    cspell+: {
      ignorePaths+: [
        '*.patch',
        '.docs/*.tag.xml',
        '.docs/*.tags',
        'tools/goodjbt.json',
        'tools/goodrb.json',
      ],
    },
  },
  prettierignore+: ['*.cc', '*.inc', '*.patch', '*.tags'],
  cz+: {
    commitizen+: {
      version_files+: [
        'man/deploy-rb.1',
        'man/jbt.1',
        'man/unjbt.1',
        'sh.tat.bfcodec.yml',
        'snapcraft.yaml',
      ],
    },
  },
  vcpkg+: {
    dependencies+: ['argparse', 'libplist', 'libzip', 'nlohmann-json', 'spdlog'],
  },
  gitignore+: ['/.deploy-rb*/'],
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
+ {
  want_scan_build: true,
  github+: { workflows+: {
    scan_build+: {
      checkout_options: { 'fetch-depth': 0 },
      configure_command: 'cmake -S . -B build-scan -DCMAKE_BUILD_TYPE=Debug',
      documentation_steps: [
        {
          name: 'Install dependencies',
          run: |||
            sudo apt-get update
            sudo apt-get install -y doxygen graphviz
          |||,
        },
        {
          name: 'Configure',
          run: 'cmake -DCMAKE_BUILD_TYPE=Release -B "$GITHUB_WORKSPACE/build" --install-prefix "$GITHUB_WORKSPACE/build/prefix" -DBUILD_DOCS_ONLY=ON',
        },
        {
          name: 'Build documentation',
          run: 'cmake --build "$GITHUB_WORKSPACE/build" --verbose --config Release --target docs',
        },
      ],
      env: {
        CMAKE_PREFIX_PATH: '${{ github.workspace }}/.wiswa-ci/scan-build/prefix',
      },
      setup_steps: [
        {
          name: 'Install build dependencies',
          run: |||
            sudo apt-get update
            sudo apt-get install -y libplist-dev libspdlog-dev libssl-dev libzip-dev nlohmann-json3-dev
          |||,
        },
        {
          name: 'Install current CMake',
          run: 'pipx install cmake',
        },
        {
          name: 'Build spdlog with bundled fmt for Clang',
          run: |||
            git clone --revision=486b55554f11c9cccc913e11a87085b2a91f706f \
              https://github.com/gabime/spdlog .wiswa-ci/scan-build/spdlog
            cmake -S .wiswa-ci/scan-build/spdlog -B .wiswa-ci/scan-build/spdlog-build \
              -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$CMAKE_PREFIX_PATH" \
              -DSPDLOG_BUILD_EXAMPLE=OFF -DSPDLOG_FMT_EXTERNAL=OFF
            cmake --build .wiswa-ci/scan-build/spdlog-build --parallel 2
            cmake --install .wiswa-ci/scan-build/spdlog-build
          |||,
        },
      ],
      workflow_file: 'pages.yml',
    },
  } },
}
