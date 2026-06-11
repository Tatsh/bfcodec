#!/usr/bin/env python3
"""Parser for REFLEC BEAT plus ``note_bas`` / ``note_med`` / ``note_har`` chart files.

These files are the per-difficulty note charts found inside each decrypted ``.rb`` song
package (entries ``note_bas`` = Basic, ``note_med`` = Medium, ``note_har`` = Hard). The
layout below was recovered from the 4.1.0 binary in Ghidra; the relevant functions are
``ParseRbffNoteFile`` (magic + version dispatch), ``ParseRbffNotesV10`` (the v10-13 body
parser), ``ReadRbffNoteRecord`` (one note record) and ``ReadRbffEvent`` (one tempo event).

All integers are little-endian.

File header (0x2C bytes)::

    +0x00  char[4]  magic          "RBFF"
    +0x04  u32      version        format version (this parser handles 10-13; samples are 11)
    +0x08  u32      reserved0      observed 0
    +0x0c  u32      reserved1      observed 0
    +0x10  u32      const0x10      the chart BPM as a float (0x43000000 == 128.0f, matching the
                                   song's BpmMin/BpmMax in info)
    +0x14  u32      field0x14      observed 0x00016A8D (constant across all three samples)
    +0x18  u32      field0x18      observed 0
    +0x1c  u16      note_count     number of note records that follow
    +0x1e  u16      event_count    number of tempo/timing events after the notes
    +0x20  u16      field0x20      small count (122/52/60 in the samples)
    +0x22  u8[10]   reserved2      observed 0

Note record (variable length, minimum 0x26 = 38 bytes), read by ``ReadRbffNoteRecord``::

    +0x00  s32      time_a         appear time in MILLISECONDS (when the note spawns on screen)
    +0x04  s32      time_b         scroll/travel time in MILLISECONDS; the note must be hit at
                                   time_a + time_b (exposed as hit_time_ms). Hit times land on the
                                   musical beat grid (e.g. 1875 ms == one 4/4 measure at 128 BPM).
                                   The engine multiplies these by 60/1000 to convert ms -> 60 fps
                                   frames (DAT_0011d090 / DAT_0011d08c in InstallParsedNotes).
    +0x08  u16      index          sequential note index (0, 1, 2, ...)
    +0x0a  u16      link           paired-note id, 0xFFFF = none (links long-note halves)
    +0x0c  s16      path_count     number of path points that follow (<= 0 means none)
    --     s16[path_count]         path_points (only present when path_count > 0)
    +..    u8       e
    +..    u8       f
    +..    u8       g
    +..    u8       h              ``efgh``: four small per-note bytes the engine fixed-point-
                                   converts into four lanes the runtime reads as the note's side
                                   and two type flags. All zero in the shipped single-player charts.
    +..    s16      param0
    +..    s16      param1
    +..    s16      param2
    +..    s16      param3         four signed params that feed the note's route/path computation
    +..    u32      flags          note role bitfield; see the flag table below
    +..    u8       m
    +..    u8       o
    +..    u16      p
    +..    u32      q              ``reserved_tail``: read from disk but the v11 installer discards
                                   m/o/p/q; all zero in every shipped chart
    --     s16[6]   extra          present only when (flags & 0x08); chain/long-note linkage. The
                                   first two s16 are prev_chain_index and next_chain_index (-1 =
                                   head / tail), forming a doubly-linked list of chain segments
                                   that the engine resolves by index (FUN_001212fc). field2..field5
                                   carry chain visual parameters.

The ``flags`` bitfield (decoded into ``flags_decoded``); the overall kind is summarised in
``note_kind`` with priority chain > green > reflect > normal::

    0x01  reflect                   reflect note (bounces to the opponent). Inferred: never set
                                    together with the chain bit, and the one flag the loader ignores
    0x02  flag_0x02                 never set in the shipped charts (reserved)
    0x04  excluded_from_score_count not tallied as a scoreable object by the engine
    0x08  chain                     chain-run member; carries the prev/next link ``extra`` block.
                                    Linked notes step by one 16th note; count rises with difficulty
    0x10  green                     PROVISIONAL green-target note; set-bit count == header
                                    field_0x20, but it covers ~36% of basic so the label is unsure
    0x20  side_object_counted       incremented into the engine's per-side object counter
    0x40  runtime_overlap           set by the engine at load time; never present on disk
    0x80  flag_0x80                 never set in the shipped charts (reserved)

Tempo event (0x24 = 36 bytes), read by ``ReadRbffEvent``::

    +0x00  u16      type           event type (type 3 adjusts the running tempo cursor)
    +0x02  u8[34]   payload        remaining event bytes (kept raw here)

Usage::

    python3 rbff_note_parser.py note_bas [note_med ...]

Each file produces JSON Lines on stdout: one ``{"kind": "header", ...}`` object, then one
``{"kind": "note", ...}`` per note, then one ``{"kind": "event", ...}`` per event, and a
final ``{"kind": "summary", ...}`` that reports whether every byte was consumed.
"""

from __future__ import annotations

import argparse
import json
import struct
import sys
from dataclasses import dataclass

HEADER_SIZE = 0x2C
MAGIC = b'RBFF'
FLAG_HAS_EXTRA = 0x08
EVENT_SIZE = 0x24

# Note ``flags`` bits. The named bits were recovered from InstallParsedNotes (FUN_0011cd44) and
# confirmed by correlating against the shipped charts (e.g. the 0x10 count equals the header's
# field_0x20, and the 0x08 count equals the number of records carrying the 12-byte tail). Bits
# never seen set on disk (0x02/0x40/0x80) and 0x01 (whose role is not pinned down) keep raw names.
FLAG_FIELDS = (
    (0x01, 'reflect'),  # reflect note (bounces back to the opponent); inferred
    (0x02, 'flag_0x02'),  # never set in the shipped charts
    (0x04, 'excluded_from_score_count'),  # not tallied as a scoreable object by the engine
    (0x08, 'chain'),  # chain-run member; carries the prev/next link block
    (0x10, 'green'),  # green-target note; set-bit count == header field_0x20
    (0x20, 'side_object_counted'),  # incremented into the per-side object counter
    (0x40, 'runtime_overlap'),  # engine-set at load time, never present on disk
    (0x80, 'flag_0x80')  # never set in the shipped charts
)

FLAG_REFLECT = 0x01
FLAG_CHAIN = 0x08
FLAG_GREEN = 0x10


def classify_note(flags):
    """Return the note's gameplay kind: ``chain``, ``green``, ``reflect`` or ``normal``.

    - ``chain`` (0x08): a member of a chain run. These carry the prev/next link block (``extra``)
      and step by one 16th note (117 ms at 128 BPM), confirmed by reconstructing the links; their
      count rises with difficulty (8/4/16 for basic/medium/hard).
    - ``reflect`` (0x01): inferred reflect note. Never set together with the chain bit (0 overlap
      across every shipped chart) and the one flag the loader never reads.
    - ``green`` (0x10): PROVISIONAL. Structurally it is the set the header ``field_0x20`` counts,
      but it covers ~36% of the basic chart, too many for a true green-target note, so it is not
      trusted yet.
    - ``normal``: everything else.

    A dedicated ``hold`` (single long-press) kind was not identified in the one song analysed; it
    may use a different flag or be absent from these charts.
    """
    if flags & FLAG_CHAIN:
        return 'chain'
    if flags & FLAG_GREEN:
        return 'green'
    if flags & FLAG_REFLECT:
        return 'reflect'
    return 'normal'


@dataclass
class Cursor:
    """A little-endian byte reader that tracks its own offset."""

    data: bytes
    offset: int = 0

    def take(self, fmt: str) -> tuple:
        size = struct.calcsize('<' + fmt)
        if self.offset + size > len(self.data):
            raise EOFError(f'Need {size} bytes at 0x{self.offset:x}, past end of file.')
        values = struct.unpack_from('<' + fmt, self.data, self.offset)
        self.offset += size
        return values

    def one(self, fmt: str):
        return self.take(fmt)[0]


def parse_header(data: bytes) -> dict:
    if data[:4] != MAGIC:
        raise ValueError(f'Not an RBFF file (magic is {data[:4]!r}).')
    (version, reserved0, reserved1, bpm_bits, field0x14, field0x18, note_count, event_count,
     field0x20) = struct.unpack_from('<IIIIIIHHH', data, 4)
    return {
        'kind': 'header',
        'magic': MAGIC.decode('ascii'),
        'version': version,
        'reserved0': reserved0,
        'reserved1': reserved1,
        'bpm': struct.unpack('<f', struct.pack('<I', bpm_bits))[0],
        'field_0x14': field0x14,
        'field_0x18': field0x18,
        'note_count': note_count,
        'event_count': event_count,
        'field_0x20': field0x20
    }


def parse_note(cursor: Cursor, ordinal: int) -> dict:
    start = cursor.offset
    time_a = cursor.one('i')
    time_b = cursor.one('i')
    index = cursor.one('H')
    link = cursor.one('H')
    path_count = cursor.one('h')
    path_points = list(cursor.take(f'{path_count}h')) if path_count > 0 else []
    e, f, g, h = cursor.take('4B')
    p0, p1, p2, p3 = cursor.take('4h')
    flags = cursor.one('I')
    m = cursor.one('B')
    o = cursor.one('B')
    p = cursor.one('H')
    q = cursor.one('I')
    extra = None
    if flags & FLAG_HAS_EXTRA:
        chain = cursor.take('6h')
        extra = {
            'prev_chain_index': None if chain[0] == -1 else chain[0],
            'next_chain_index': None if chain[1] == -1 else chain[1],
            'field2': chain[2],
            'field3': chain[3],
            'field4': chain[4],
            'field5': chain[5]
        }
    return {
        'kind': 'note',
        'note_kind': classify_note(flags),
        'ordinal': ordinal,
        'file_offset': start,
        'byte_length': cursor.offset - start,
        'time_a_ms': time_a,
        'time_b_ms': time_b,
        'hit_time_ms': time_a + time_b,
        'index': index,
        'link': None if link == 0xFFFF else link,
        'path_count': path_count,
        'path_points': path_points,
        'efgh': {
            'e': e,
            'f': f,
            'g': g,
            'h': h
        },
        'params': {
            'param0': p0,
            'param1': p1,
            'param2': p2,
            'param3': p3
        },
        'flags': flags,
        'flags_hex': f'0x{flags:08x}',
        'flags_decoded': {
            name: bool(flags & bit)
            for bit, name in FLAG_FIELDS
        },
        'has_extra': bool(flags & FLAG_HAS_EXTRA),
        # m/o/p/q are read from the on-disk record but the v11 installer (InstallParsedNotes)
        # never copies them into the runtime note; they are 0 across every shipped chart. Kept
        # here for fidelity to the byte layout. They are likely consumed by another format version.
        'reserved_tail': {
            'm': m,
            'o': o,
            'p': p,
            'q': q
        },
        'extra': extra
    }


def parse_event(cursor: Cursor, ordinal: int) -> dict:
    start = cursor.offset
    payload = cursor.take(f'{EVENT_SIZE}B')
    event_type = struct.unpack_from('<H', bytes(payload), 0)[0]
    return {
        'kind': 'event',
        'ordinal': ordinal,
        'file_offset': start,
        'type': event_type,
        'payload_hex': bytes(payload).hex()
    }


def parse_file(path: str):
    with open(path, 'rb') as handle:
        data = handle.read()
    header = parse_header(data)
    yield header
    cursor = Cursor(data, HEADER_SIZE)
    for ordinal in range(header['note_count']):
        yield parse_note(cursor, ordinal)
    for ordinal in range(header['event_count']):
        yield parse_event(cursor, ordinal)
    yield {
        'kind': 'summary',
        'file': path,
        'file_size': len(data),
        'bytes_consumed': cursor.offset,
        'fully_consumed': cursor.offset == len(data),
        'trailing_bytes': len(data) - cursor.offset
    }


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('files', nargs='+', help='note_bas / note_med / note_har files.')
    args = parser.parse_args(argv)
    exit_code = 0
    for path in args.files:
        for record in parse_file(path):
            sys.stdout.write(json.dumps(record) + '\n')
            if record.get('kind') == 'summary' and not record['fully_consumed']:
                exit_code = 1
    return exit_code


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
