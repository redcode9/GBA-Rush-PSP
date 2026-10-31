#!/usr/bin/env python3
"""Small FireRed/LeafGreen save helper for the end-to-end link test.

  place SRC DST GROUP MAP X Y   copy SRC to DST so that Continue warps the
                                player to that map spot (the game's own
                                continue-game warp)
  place-heal SRC DST            the same, to the spot outside the last
                                Pokemon Center the player used
  party SAVE                    print the species of the party, in order
  check-trade HOST0 HOST1 JOIN0 JOIN1
                                check saves before and after a trade of the
                                host's first Pokemon for the joiner's second
"""
import struct
import sys

# A save holds two slots of 14 sectors of 4 KiB; each sector ends with a
# footer at 0xFF4 (section id, checksum, signature, save counter). SIZES are
# the checksummed data sizes of section 0 (SaveBlock2) and section 1 (the
# start of SaveBlock1), the only sections edited here.
SECTOR = 0x1000
SIZES = {0: 0xF24, 1: 0xF80}
# Substructure order for personality % 24: G=growth (holds the species).
ORDERS = ["GAEM", "GAME", "GEAM", "GEMA", "GMAE", "GMEA", "AGEM", "AGME",
          "AEGM", "AEMG", "AMGE", "AMEG", "EGAM", "EGMA", "EAGM", "EAMG",
          "EMGA", "EMAG", "MGAE", "MGEA", "MAGE", "MAEG", "MEGA", "MEAG"]


def newest_sections(data):
    """Map section id to file offset in the slot with the newest counter."""
    best = None
    for slot in (0, 1):
        sections = {}
        for i in range(14):
            off = (slot * 14 + i) * SECTOR
            sid, _, _, counter = struct.unpack_from('<HHII', data, off + 0xFF4)
            sections[sid] = (off, counter)
        counter = max(c for _, c in sections.values())
        if best is None or counter > best[0]:
            best = (counter, sections)
    return {sid: off for sid, (off, _) in best[1].items()}


def fix_checksum(data, off, size):
    """Sum the section's words and fold the total into 16 bits at 0xFF6."""
    total = sum(struct.unpack_from('<%dI' % (size // 4), data, off))
    struct.pack_into('<H', data, off + 0xFF6, ((total >> 16) + total) & 0xFFFF)


def place(src, dst, group, mapnum, x, y):
    data = bytearray(open(src, 'rb').read())
    sections = newest_sections(data)
    data[sections[0] + 9] |= 1          # specialSaveWarpFlags: continue warp
    # SaveBlock1.continueGameWarp: map group, map number, warp id -1, x, y.
    struct.pack_into('<bbbxhh', data, sections[1] + 12, group, mapnum, -1, x, y)
    fix_checksum(data, sections[0], SIZES[0])
    fix_checksum(data, sections[1], SIZES[1])
    open(dst, 'wb').write(data)


def party(path):
    """Species of each party Pokemon, decrypted from the save.

    SaveBlock1 holds the party count at 0x34 and 100-byte Pokemon from 0x38.
    Their four 12-byte substructures at offset 32 are XORed with
    personality ^ OT id and ordered by personality % 24.
    """
    data = open(path, 'rb').read()
    block1 = newest_sections(data)[1]
    count = data[block1 + 0x34]
    species = []
    for i in range(count):
        mon = block1 + 0x38 + i * 100
        personality, ot_id = struct.unpack_from('<II', data, mon)
        key = personality ^ ot_id
        growth = ORDERS[personality % 24].index('G')
        word = struct.unpack_from('<I', data, mon + 32 + growth * 12)[0] ^ key
        species.append(word & 0xFFFF)
    return species


def last_heal(path):
    """SaveBlock1.lastHealLocation (0x1C), outside the last Pokemon Center."""
    data = open(path, 'rb').read()
    block1 = newest_sections(data)[1]
    group, mapnum, _, x, y = struct.unpack_from('<bbbxhh', data, block1 + 0x1C)
    return group, mapnum, x, y


if __name__ == '__main__':
    if sys.argv[1] == 'place':
        place(sys.argv[2], sys.argv[3], *map(int, sys.argv[4:8]))
    elif sys.argv[1] == 'check-trade':
        # Host slot 0 now holds the joiner's second Pokemon and the joiner's
        # slot 1 holds the host's first; every other slot is unchanged.
        host0, host1, join0, join1 = (party(p) for p in sys.argv[2:6])
        expected_host = [join0[1]] + host0[1:]
        expected_join = join0[:1] + [host0[0]] + join0[2:]
        if host1 != expected_host or join1 != expected_join:
            print('host   %s -> %s, expected %s' % (host0, host1, expected_host))
            print('joiner %s -> %s, expected %s' % (join0, join1, expected_join))
            sys.exit(1)
    elif sys.argv[1] == 'place-heal':
        place(sys.argv[2], sys.argv[3], *last_heal(sys.argv[2]))
    elif sys.argv[1] == 'party':
        print(' '.join(str(s) for s in party(sys.argv[2])))
