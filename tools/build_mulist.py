#!/usr/bin/env python3
"""Build a complete ``mulist`` from song packages and deploy it to an iOS device over SSH.

For each ``.rb`` (REFLEC BEAT plus) or ``.jbt`` (jubeat) package given on the command line, this
tries every passphrase for that extension with ``unjbt --mulist-entry`` until one decrypts the
package's ``info`` entry and prints a ``<dict>`` mulist entry. The collected entries are wrapped in
the standard property-list ``<array>`` to form a ``mulist`` document, which is then encrypted with
``bfc`` using the key derived from the device's Keychain UUID (``--uuid``). Finally the encrypted
``mulist`` and the renamed song packages are copied to the application container over SSH.

The fixed IV baked into ``unjbt`` and ``bfc`` (``E36631DA2C85A064``) is used throughout, so it is
never supplied here.

Usage::

    build_mulist.py --uuid <device-uuid> --app-uuid <app-uuid> --host <host> \\
        100040001_RqrX.rb 200010002_aB3c.jbt

The encrypted ``mulist`` lands in ``Documents`` and each song package lands in
``Library/Private Documents`` inside the container named by ``--app-uuid``. Pass ``--dry-run``
(``-y``) to simulate every step, creating no files and only logging the commands that would run.
"""

from __future__ import annotations

from collections.abc import Sequence
from pathlib import Path
import argparse
import logging
import random
import re
import shlex
import shutil
import subprocess
import sys

logger = logging.getLogger(__name__)

KEYS_BY_EXTENSION = {
    '.rb': (
        'Konami ReflecBeat For iOS.',  # DecodeType 0, content bundled with the app.
        'Konami ReflecBeatplus.',  # DecodeType 1, downloaded DLC songs.
        'REFLECBEATplus lovers.' # World version (does not have DecodeType 1).
    ),
    '.jbt': ('Konami Bemani Mobile iPad', 'jubeatskmpledata', 'Konami Bemani Mobile iOS')
}
"""
Passphrases to try, in order, keyed by the package extension. Each candidate is handed to unjbt as
-K; the first that decrypts the package's info entry wins. The trailing dots in the REFLEC BEAT
keys are part of the passphrase, not punctuation.
"""

_MULIST_HEADER = ('<?xml version="1.0" encoding="UTF-8"?>\n'
                  '<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" '
                  '"http://www.apple.com/DTDs/PropertyList-1.0.dtd">\n'
                  '<plist version="1.0">\n'
                  '<array>\n')
_MULIST_FOOTER = '</array>\n</plist>\n'

REMOTE_CONTAINER_ROOT = '/var/mobile/Containers/Data/Application'
"""
Root of the per-application data containers on a jailbroken device. The app-specific UUID and the
Documents / Library subdirectories are appended to this.
"""

SSH_AUTH_OPTIONS = ('-F', '/dev/null', '-o', 'KbdInteractiveAuthentication=yes', '-o',
                    'PreferredAuthentications=publickey,password,keyboard-interactive', '-o',
                    'PasswordAuthentication=yes')
"""
Authentication options forced onto every ssh and scp call so the user's own ssh_config and the
device's typical lack of key setup do not get in the way. ``-F /dev/null`` ignores the user's
ssh_config, while password and keyboard-interactive authentication are enabled; public-key
authentication stays first so existing keys are still used without a prompt when they work.
"""

LEADING_DIGITS = re.compile(r'\d+')
"""
Leading run of digits in a package filename, used to discard the per-song token suffix (for
example 100040001_RqrX.rb becomes 100040001.rb).
"""


def candidate_keys(path: Path) -> tuple[str, ...]:
    """Return the passphrases to try for a package, chosen by its extension."""
    return KEYS_BY_EXTENSION.get(path.suffix.lower(), ())


def trimmed_name(path: Path) -> str:
    """
    Return the on-device filename: the leading digits joined with the lowercased extension.

    When the filename does not start with digits the original name is kept unchanged so that
    nothing is silently lost.
    """
    match = LEADING_DIGITS.match(path.stem)
    if not match:
        logger.warning('Keeping %s as-is; it does not start with digits.', path.name)
        return path.name
    return f'{match.group()}{path.suffix.lower()}'


def extract_entry(unjbt: str, package: Path) -> str | None:
    """
    Try each known key for the package and return the printed mulist ``<dict>`` entry.

    Returns ``None`` when no candidate key decrypts the package. The last error from ``unjbt`` is
    surfaced as a warning so the cause (a wrong key, a missing info entry, or a build without
    libplist) is visible rather than guessed at.
    """
    keys = candidate_keys(package)
    if not keys:
        logger.warning('Package %s has an unrecognised extension; no keys to try.', package.name)
        return None
    last_error = ''
    for key in keys:
        logger.debug('Trying key %r for %s.', key, package.name)
        result = subprocess.run((unjbt, '--mulist-entry', '-K', key, str(package)),
                                capture_output=True,
                                text=True,
                                check=False)
        if result.returncode == 0 and '<dict>' in result.stdout:
            logger.debug('Decrypted %s with key %r.', package.name, key)
            return result.stdout.strip('\n')
        last_error = result.stderr.strip() or last_error
    logger.warning('No known key worked for %s (%s).', package.name,
                   last_error or 'no diagnostic output')
    return None


def build_mulist(entries: list[str]) -> str:
    """Wrap the collected ``<dict>`` entries in the property-list array envelope."""
    body = ''.join(f'{entry}\n' for entry in entries)
    return f'{_MULIST_HEADER}{body}{_MULIST_FOOTER}'


def run_command(command: Sequence[str], *, dry_run: bool, verbose: bool) -> None:
    """Run a command, or log it when ``dry_run`` is set. Raises on a non-zero exit."""
    rendered = shlex.join(command)
    if dry_run:
        logger.info('Would run: %s', rendered)
        return
    logger.log(logging.INFO if verbose else logging.DEBUG, 'Running: %s', rendered)
    subprocess.run(command, check=True)


def ssh_prefix(option: str, port: int, identity: str | None) -> tuple[str, ...]:
    """
    Return the leading ``ssh``/``scp`` arguments shared by every remote call.

    ``option`` is the port flag, which differs between the tools (``-p`` for ssh, ``-P`` for scp).
    """
    prefix = (option, str(port), *SSH_AUTH_OPTIONS)
    if identity:
        return (*prefix, '-i', identity)
    return prefix


def deploy(*, mulist_path: Path, song_paths: list[Path], user: str, host: str, app_uuid: str,
           port: int, identity: str | None, dry_run: bool, verbose: bool) -> None:
    """Create the remote directories and copy the mulist and song packages into the container."""
    target = f'{user}@{host}'
    container = f'{REMOTE_CONTAINER_ROOT}/{app_uuid}'
    documents = f'{container}/Documents'
    private_documents = f'{container}/Library/Private Documents'

    # A single mkdir -p ensures both destinations exist. The paths are quoted because the remote
    # shell, not Python, splits the command, and Private Documents contains a space.
    run_command(('ssh', *ssh_prefix('-p', port, identity), target, 'mkdir', '-p',
                 documents, private_documents),
                dry_run=dry_run,
                verbose=verbose)
    run_command(('scp', *ssh_prefix('-P', port, identity), str(mulist_path),
                 f'{target}:{documents + "/"}'),
                dry_run=dry_run,
                verbose=verbose)
    if song_paths:
        run_command(('scp', *ssh_prefix('-P', port, identity), *(str(p) for p in song_paths),
                     f'{target}:{private_documents + "/"}'),
                    dry_run=dry_run,
                    verbose=verbose)


def resolve_tool(override: str | None, name: str) -> str:
    """Return the path to a helper tool, honouring an override and otherwise searching ``PATH``."""
    if override:
        logger.debug('Using %s override: %s.', name, override)
        return override
    found = shutil.which(name)
    if not found:
        logger.error('Cannot find %s on PATH; pass --%s with its path.', name, name)
        sys.exit(2)
    logger.debug('Resolved %s to %s.', name, found)
    return found


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('packages', nargs='+', help='The .rb / .jbt packages to include.')
    parser.add_argument('--uuid',
                        required=True,
                        help="Device Keychain UUID; the mulist key is MD5 of its uppercase form.")
    parser.add_argument('--app-uuid',
                        required=True,
                        help='Application container UUID naming the remote directory.')
    parser.add_argument('--host', required=True, help='SSH host of the device.')
    parser.add_argument('--user', default='mobile', help='SSH user (default: mobile).')
    parser.add_argument('--port', type=int, default=22, help='SSH port (default: 22).')
    parser.add_argument('--identity', help='SSH identity (private key) file.')
    parser.add_argument('--staging-dir',
                        type=Path,
                        default=Path('mulist-out'),
                        help='Local directory for the built mulist and renamed packages.')
    parser.add_argument('--unjbt', help='Path to the unjbt tool (default: search PATH).')
    parser.add_argument('--bfc', help='Path to the bfc tool (default: search PATH).')
    parser.add_argument('-y',
                        '--dry-run',
                        action='store_true',
                        help='Simulate every step, creating no files and only logging commands.')
    parser.add_argument('-v', '--verbose', action='store_true', help='Report each step.')
    parser.add_argument('-d',
                        '--debug',
                        action='store_true',
                        help='Enable debug-level logging.')
    return parser.parse_args(argv)


def main(argv: Sequence[str]) -> int:
    args = parse_args(argv)
    logging.basicConfig(level=logging.DEBUG if args.debug else logging.INFO,
                        format='%(levelname)s: %(message)s' if args.debug else '%(message)s')
    unjbt = resolve_tool(args.unjbt, 'unjbt')
    bfc = resolve_tool(args.bfc, 'bfc')

    staging = args.staging_dir
    if args.dry_run:
        logger.info('Would create staging directory: %s.', staging)
    else:
        staging.mkdir(parents=True, exist_ok=True)
        logger.debug('Staging directory: %s.', staging)

    entries: list[str] = []
    song_paths: list[Path] = []
    skipped = 0
    seen_names: set[str] = set()
    for raw in args.packages:
        package = Path(raw)
        if not package.is_file():
            logger.warning('Package %s is not a file; skipping.', package)
            skipped += 1
            continue
        logger.debug('Processing %s.', package)
        # The entry is read from the original filename so its ItemURL keeps the download token; the
        # local copy is renamed only for on-device storage.
        entry = extract_entry(unjbt, package)
        if entry is None:
            skipped += 1
            continue
        name = trimmed_name(package)
        if name in seen_names:
            logger.warning('Package %s trims to %s, already staged; skipping the duplicate.',
                           package.name, name)
            skipped += 1
            continue
        seen_names.add(name)
        destination = staging / name
        if args.dry_run:
            logger.info('Would stage %s as %s.', package, destination)
        else:
            shutil.copyfile(package, destination)
            logger.debug('Staged %s as %s.', package, destination)
        song_paths.append(destination)
        entries.append(entry)

    if not entries:
        logger.warning('No packages could be decrypted; refusing to build an empty mulist.')
        return 1

    plaintext = staging / 'mulist.plist'
    if args.dry_run:
        logger.info('Would write plaintext mulist with %d entries to %s.', len(entries), plaintext)
    else:
        plaintext.write_bytes(random.randbytes(4) + build_mulist(entries).encode())
        logger.debug('Wrote plaintext mulist with %d entries to %s.', len(entries), plaintext)
    encrypted = staging / 'mulist'
    # bfc derives the key from the device UUID and uses the same default IV the device expects.
    run_command((bfc, '--uuid', args.uuid, '-o', str(encrypted), str(plaintext)),
                dry_run=args.dry_run,
                verbose=args.verbose)

    deploy(mulist_path=encrypted,
           song_paths=song_paths,
           user=args.user,
           host=args.host,
           app_uuid=args.app_uuid,
           port=args.port,
           identity=args.identity,
           dry_run=args.dry_run,
           verbose=args.verbose)

    entry_word = 'entry' if len(entries) == 1 else 'entries'
    package_word = 'package' if len(song_paths) == 1 else 'packages'
    if args.dry_run:
        logger.info('Dry run: would build %s with %d %s and deploy %d %s.', encrypted,
                    len(entries), entry_word, len(song_paths), package_word)
    else:
        logger.info('Built %s with %d %s; deployed %d %s.', encrypted, len(entries), entry_word,
                    len(song_paths), package_word)
    if skipped:
        logger.warning('Skipped %d %s.', skipped, 'package' if skipped == 1 else 'packages')
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
