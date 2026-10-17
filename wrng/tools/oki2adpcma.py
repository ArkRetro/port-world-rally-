#!/usr/bin/env python3
"""oki2adpcma.py - World Rally (OKI M6295) -> Neo Geo YM2610 ADPCM-A V-ROM.

Usage:
  oki2adpcma.py <worldr14.c01> <worldr15.c03> <wr_cpu.bin> <out.v1> <out samples.inc>
                <out map.csv> [--wav DIR] [--interp linear|cubic]

Input
  * worldr14.c01 / worldr15.c03 : the two 512 KB OKI ROMs (OKI space 0x00000-0x7FFFF and
    0x80000-0xFFFFF of the 1 MB sample ROM).  The OKI runs at 1 MHz, pin 7 high
    (1e6/132 = 7575.76 Hz).  OKI map: 0x00000-0x2FFFF fixed, 0x30000-0x3FFFF = bank window
    (bank n = ROM n*0x10000 .. n*0x10000+0xFFFF, latch 70000D).
  * wr_cpu.bin : 68000 program ROM in CPU order.  The game's phrase table lives at 0x144AC:
    60 entries x 2 bytes (bank, phrase); phrase 0 (or an invalid phrase) means "empty".
Output
  * <out.v1>        : V-ROM.  Block 0 (256 bytes) is encoded silence, every phrase starts on
                      a 256-byte boundary and is padded with encoded silence to a multiple of
                      256 bytes (the YM2610 end address is inclusive of its 256-byte block).
  * <samples.inc>   : 60 lines ".db lo(start>>8), hi(start>>8), lo(end>>8), hi(end>>8)" in the
                      order of the 0x144AC table (empty entries: 0,0,0,0) for sdasz80.
  * <map.csv>       : index, bank, phrase, OKI addresses, sample counts, duration, V-ROM
                      addresses, RMS error.  A .json with the same data is written next to it.
  * --wav DIR       : per phrase, DIR/phNN_src.wav (resampled OKI PCM) and DIR/phNN_adpcma.wav
                      (decoded back from the ADPCM-A that was written), both 18518 Hz 16-bit.

Encoding
  OKI ADPCM is decoded exactly like MAME's oki_adpcm_state (12-bit signal, clamp, step table
  floor(16*1.1^n)), resampled from 7575.76 Hz to 18518.52 Hz (YM2610 ADPCM-A fixed rate
  = 8 MHz / 432) with Catmull-Rom cubic (or linear) interpolation, then encoded to YM2610
  ADPCM-A by running the ymfm adpcm_a_channel decoder as the encoder's state and choosing,
  for every sample, the nibble whose decoded output is closest to the target (greedy search,
  high nibble of each byte first, 12-bit wrapping accumulator, step index 0..48 with
  increments -1,-1,-1,-1,2,5,7,9).
"""
import json
import os
import struct
import sys

try:
    import numpy as np
except ImportError:  # pragma: no cover - pure python fallback
    np = None

OKI_RATE = 1_000_000 / 132.0            # 7575.7576 Hz (pin 7 high)
ADPCMA_RATE = 8_000_000 / 432.0         # 18518.52 Hz (55.5 kHz / 3)
PHRASE_TABLE_68K = 0x144AC
PHRASE_TABLE_ENTRIES = 60
OKI_BANK_WINDOW = 0x30000
VROM_MAX = 16 * 1024 * 1024
BLOCK = 256                              # YM2610 ADPCM-A address granularity (bytes)


# ---------------------------------------------------------------- OKI M6295 (MAME okiadpcm.cpp)

def _oki_tables():
    nbl2bit = [[1, 0, 0, 0], [1, 0, 0, 1], [1, 0, 1, 0], [1, 0, 1, 1],
               [1, 1, 0, 0], [1, 1, 0, 1], [1, 1, 1, 0], [1, 1, 1, 1],
               [-1, 0, 0, 0], [-1, 0, 0, 1], [-1, 0, 1, 0], [-1, 0, 1, 1],
               [-1, 1, 0, 0], [-1, 1, 0, 1], [-1, 1, 1, 0], [-1, 1, 1, 1]]
    diff = []
    for step in range(49):
        stepval = int(16.0 * (11.0 / 10.0) ** step)      # floor(16 * 1.1^step)
        row = []
        for nib in range(16):
            b = nbl2bit[nib]
            row.append(b[0] * (stepval * b[1] + (stepval // 2) * b[2] + (stepval // 4) * b[3] + stepval // 8))
        diff.append(row)
    return diff


OKI_DIFF = _oki_tables()
OKI_INDEX_SHIFT = [-1, -1, -1, -1, 2, 4, 6, 8]


def oki_decode(nibbles):
    """Exact MAME oki_adpcm_state decoder. Returns a list of 12-bit signed samples (-2048..2047)."""
    signal = 0
    step = 0
    out = []
    diff = OKI_DIFF
    shift = OKI_INDEX_SHIFT
    for nib in nibbles:
        signal += diff[step][nib]
        if signal > 2047:
            signal = 2047
        elif signal < -2048:
            signal = -2048
        step += shift[nib & 7]
        if step > 48:
            step = 48
        elif step < 0:
            step = 0
        out.append(signal)
    return out


class OkiRom:
    def __init__(self, data):
        assert len(data) == 0x100000, "OKI ROM must be 1 MB"
        self.data = data

    def phys(self, addr, bank):
        """Translate an OKI-space address (0..0x3FFFF) to a physical ROM offset."""
        addr &= 0x3FFFF
        if addr >= OKI_BANK_WINDOW:
            return (bank & 15) * 0x10000 + (addr - OKI_BANK_WINDOW)
        return addr

    def phrase(self, index, bank):
        """Returns (start, stop, phys_start, phys_stop) or None for an invalid phrase."""
        base = index * 8
        start = (self.data[base] << 16) | (self.data[base + 1] << 8) | self.data[base + 2]
        stop = (self.data[base + 3] << 16) | (self.data[base + 4] << 8) | self.data[base + 5]
        start &= 0x3FFFF
        stop &= 0x3FFFF
        if start >= stop:
            return None
        return start, stop, self.phys(start, bank), self.phys(stop, bank)

    def nibbles(self, start, stop, bank):
        """Nibble stream (high nibble first) of OKI addresses start..stop inclusive, like MAME."""
        out = []
        for a in range(start, stop + 1):
            b = self.data[self.phys(a, bank)]
            out.append(b >> 4)
            out.append(b & 15)
        return out


# ---------------------------------------------------------------- resampling

def resample(pcm, src_rate, dst_rate, method="cubic"):
    """Resample a list/array of samples with linear or Catmull-Rom cubic interpolation."""
    n = len(pcm)
    if n == 0:
        return []
    ratio = src_rate / dst_rate
    m = int(n / ratio)
    if np is not None:
        x = np.arange(m, dtype=np.float64) * ratio
        src = np.asarray(pcm, dtype=np.float64)
        if method == "linear":
            return np.interp(x, np.arange(n), src)
        i = np.floor(x).astype(np.int64)
        t = x - i
        pad = np.concatenate(([src[0]], src, [src[-1], src[-1]]))
        p0, p1, p2, p3 = pad[i], pad[i + 1], pad[i + 2], pad[i + 3]
        t2 = t * t
        t3 = t2 * t
        y = 0.5 * ((2 * p1) + (-p0 + p2) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t2 + (-p0 + 3 * p1 - 3 * p2 + p3) * t3)
        return y
    out = []
    for k in range(m):
        x = k * ratio
        i = int(x)
        t = x - i
        a = pcm[i]
        b = pcm[i + 1] if i + 1 < n else pcm[-1]
        out.append(a + (b - a) * t)
    return out


# ---------------------------------------------------------------- YM2610 ADPCM-A (ymfm_adpcm.cpp)

ADPCMA_STEPS = [16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107,
                118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544,
                598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552]
ADPCMA_STEP_INC = [-1, -1, -1, -1, 2, 5, 7, 9]
# delta for magnitude m (bits 0-2 of the nibble) at every step index
ADPCMA_DELTA = [[(2 * m + 1) * s // 8 for m in range(8)] for s in ADPCMA_STEPS]


def _sx12(v):
    v &= 0xFFF
    return v - 0x1000 if v & 0x800 else v


def adpcma_decode(data, nsamples=None):
    """ymfm adpcm_a_channel::clock(): returns 12-bit signed samples, high nibble first."""
    acc = 0
    idx = 0
    out = []
    total = len(data) * 2 if nsamples is None else nsamples
    for k in range(total):
        byte = data[k >> 1]
        nib = (byte >> 4) if (k & 1) == 0 else (byte & 15)
        delta = ADPCMA_DELTA[idx][nib & 7]
        if nib & 8:
            delta = -delta
        acc = (acc + delta) & 0xFFF
        idx += ADPCMA_STEP_INC[nib & 7]
        if idx < 0:
            idx = 0
        elif idx > 48:
            idx = 48
        out.append(acc - 0x1000 if acc & 0x800 else acc)
    return out


def adpcma_encode(samples12):
    """Greedy YM2610 ADPCM-A encoder.

    samples12: iterable of target samples in the 12-bit range (-2048..2047, may be float).
    The encoder state is the exact ymfm decoder; for each sample the nibble whose decoded
    output is nearest to the target is chosen.  Returns (bytes, decoded 12-bit samples).
    """
    acc = 0          # 12-bit unsigned accumulator, exactly as the chip
    idx = 0
    nibbles = []
    decoded = []
    deltas = ADPCMA_DELTA
    inc = ADPCMA_STEP_INC
    for target in samples12:
        # the interpolator may overshoot the 12-bit range; the chip cannot represent that
        if target > 2047:
            target = 2047
        elif target < -2048:
            target = -2048
        cur = acc - 0x1000 if acc & 0x800 else acc
        d = target - cur
        sign = 8 if d < 0 else 0
        ad = -d if d < 0 else d
        row = deltas[idx]
        # row is increasing in m: find the two magnitudes bracketing |d|
        lo = 0
        hi = 7
        while lo < hi:
            mid = (lo + hi) >> 1
            if row[mid] < ad:
                lo = mid + 1
            else:
                hi = mid
        best_m = 0
        best_e = None
        best_v = 0
        best_sign = sign
        wrapped = True
        for m in ((lo, lo - 1) if lo > 0 else (lo,)):
            dl = row[m]
            if sign:
                dl = -dl
            v = (acc + dl) & 0xFFF
            sv = v - 0x1000 if v & 0x800 else v
            e = abs(target - sv)
            if best_e is None or e < best_e:
                best_e = e
                best_m = m
                best_v = v
                wrapped = (sv - cur) != dl
        if wrapped:
            # the accumulator would wrap around (only near +-2048): exhaustive search
            for nib in range(16):
                dl = row[nib & 7]
                if nib & 8:
                    dl = -dl
                v = (acc + dl) & 0xFFF
                sv = v - 0x1000 if v & 0x800 else v
                e = abs(target - sv)
                if e < best_e:
                    best_e = e
                    best_m = nib & 7
                    best_sign = nib & 8
                    best_v = v
        sign = best_sign
        acc = best_v
        nibbles.append(sign | best_m)
        decoded.append(acc - 0x1000 if acc & 0x800 else acc)
        idx += inc[best_m]
        if idx < 0:
            idx = 0
        elif idx > 48:
            idx = 48
    if len(nibbles) & 1:
        # never happens for our block-padded input, but keep the stream well-formed
        nibbles.append(0)
    out = bytearray(len(nibbles) >> 1)
    for i in range(0, len(nibbles), 2):
        out[i >> 1] = (nibbles[i] << 4) | nibbles[i + 1]
    return bytes(out), decoded


# ---------------------------------------------------------------- helpers

def write_wav(path, samples16, rate):
    data = struct.pack("<%dh" % len(samples16), *[max(-32768, min(32767, int(round(s)))) for s in samples16])
    with open(path, "wb") as f:
        f.write(b"RIFF" + struct.pack("<I", 36 + len(data)) + b"WAVE")
        f.write(b"fmt " + struct.pack("<IHHIIHH", 16, 1, 1, int(round(rate)), int(round(rate)) * 2, 2, 16))
        f.write(b"data" + struct.pack("<I", len(data)) + data)


def rms_rel_error(ref, test):
    n = min(len(ref), len(test))
    if n == 0:
        return 0.0
    if np is not None:
        a = np.asarray(ref[:n], dtype=np.float64)
        b = np.asarray(test[:n], dtype=np.float64)
        den = float(np.sqrt(np.mean(a * a)))
        if den == 0:
            return 0.0
        return float(np.sqrt(np.mean((a - b) ** 2))) / den
    num = 0.0
    den = 0.0
    for i in range(n):
        num += (ref[i] - test[i]) ** 2
        den += ref[i] ** 2
    return (num / den) ** 0.5 if den else 0.0


def read_phrase_table(cpu):
    table = []
    for i in range(PHRASE_TABLE_ENTRIES):
        off = PHRASE_TABLE_68K + i * 2
        table.append((cpu[off], cpu[off + 1]))
    return table


# ---------------------------------------------------------------- main

def main(argv):
    wav_dir = None
    interp = "cubic"
    i = 0
    rest = []
    while i < len(argv[1:]):
        a = argv[1 + i]
        if a == "--wav":
            wav_dir = argv[2 + i]
            i += 2
            continue
        if a == "--interp":
            interp = argv[2 + i]
            i += 2
            continue
        if a.startswith("--"):
            print("unknown option", a, file=sys.stderr)
            return 2
        rest.append(a)
        i += 1
    if len(rest) != 6:
        print(__doc__, file=sys.stderr)
        return 2
    f_oki0, f_oki1, f_cpu, f_v1, f_inc, f_csv = rest

    with open(f_oki0, "rb") as f:
        oki_data = f.read()
    with open(f_oki1, "rb") as f:
        oki_data += f.read()
    rom = OkiRom(oki_data)
    with open(f_cpu, "rb") as f:
        cpu = f.read()
    table = read_phrase_table(cpu)

    if wav_dir:
        os.makedirs(wav_dir, exist_ok=True)

    # V-ROM: block 0 = encoded silence (used by empty entries whose start/end are 0)
    silence, _ = adpcma_encode([0] * (BLOCK * 2))
    vrom = bytearray(silence)

    entries = []          # one per table index
    by_phys = {}          # (phys_start, phys_stop) -> entry (dedupe identical data)
    errors = []
    print("idx bank phr  oki_start oki_stop  phys     nsamp(oki) nsamp(ym)  dur(s)  vrom_start vrom_end  rms_err")
    for idx, (bank, phrase) in enumerate(table):
        entry = {"index": idx, "bank": bank, "phrase": phrase, "empty": True}
        info = rom.phrase(phrase, bank) if 0 < phrase < 128 else None
        if info is None:
            entries.append(entry)
            print("%2d  (empty)" % idx)
            continue
        start, stop, pstart, pstop = info
        entry.update({"empty": False, "oki_start": start, "oki_stop": stop, "phys_start": pstart, "phys_stop": pstop})
        key = (pstart, pstop)
        if key in by_phys:
            ref = by_phys[key]
            for k in ("nsamp_oki", "nsamp_ym", "duration", "vrom_start", "vrom_end", "start_reg", "end_reg", "rms_err"):
                entry[k] = ref[k]
            entry["same_as"] = ref["index"]
            entries.append(entry)
            print("%2d  %2d  %02X  %05X    %05X   %06X  %8d  %8d  %6.3f   %06X    %06X  %.4f (same as %d)" % (
                idx, bank, phrase, start, stop, pstart, entry["nsamp_oki"], entry["nsamp_ym"], entry["duration"],
                entry["vrom_start"], entry["vrom_end"], entry["rms_err"], ref["index"]))
            continue

        nib = rom.nibbles(start, stop, bank)
        pcm12 = oki_decode(nib)
        pcm16 = [s * 16 for s in pcm12]
        res16 = resample(pcm16, OKI_RATE, ADPCMA_RATE, interp)
        res16 = [float(s) for s in res16]
        nsamp_ym = len(res16)
        # pad with silence to a whole number of 256-byte blocks (512 samples)
        padded = res16 + [0.0] * ((-nsamp_ym) % (BLOCK * 2))
        target12 = [s / 16.0 for s in padded]
        data, dec12 = adpcma_encode(target12)
        assert len(data) % BLOCK == 0
        dec16 = [s * 16 for s in dec12]
        err = rms_rel_error(res16, dec16[:nsamp_ym])
        errors.append(err)

        # the YM2610's ADPCM-A address counter only carries 20 bits: a sample must not cross a 1 MB
        # boundary (MAME plays it anyway, the real chip wraps inside the 1 MB bank) -> pad to the boundary
        if (len(vrom) >> 20) != ((len(vrom) + len(data) - 1) >> 20):
            vrom += bytes(((len(vrom) >> 20) + 1 << 20) - len(vrom))
        vstart = len(vrom)
        vrom += data
        vend = len(vrom) - 1
        entry.update({
            "nsamp_oki": len(pcm12), "nsamp_ym": nsamp_ym, "duration": len(pcm12) / OKI_RATE,
            "vrom_start": vstart, "vrom_end": vend, "vrom_bytes": len(data),
            "start_reg": vstart >> 8, "end_reg": vend >> 8, "rms_err": err,
        })
        by_phys[key] = entry
        entries.append(entry)
        print("%2d  %2d  %02X  %05X    %05X   %06X  %8d  %8d  %6.3f   %06X    %06X  %.4f" % (
            idx, bank, phrase, start, stop, pstart, len(pcm12), nsamp_ym, entry["duration"], vstart, vend, err))

        if wav_dir:
            write_wav(os.path.join(wav_dir, "ph%02X_src.wav" % idx), res16, ADPCMA_RATE)
            write_wav(os.path.join(wav_dir, "ph%02X_adpcma.wav" % idx), dec16, ADPCMA_RATE)

    if len(vrom) > VROM_MAX:
        print("ERROR: V-ROM is %d bytes, more than 16 MB" % len(vrom), file=sys.stderr)
        return 1
    # real cartridges (and flash carts mapping them) use power-of-two V-ROM sizes
    size = 1 << 20
    while size < len(vrom):
        size <<= 1
    vrom += bytes(size - len(vrom))
    with open(f_v1, "wb") as f:
        f.write(vrom)

    with open(f_inc, "w") as f:
        f.write("; generated by tools/oki2adpcma.py - YM2610 ADPCM-A start/end (>>8) per phrase index\n")
        for e in entries:
            if e["empty"]:
                f.write("        .db 0,0,0,0\n")
            else:
                s = e["start_reg"]
                t = e["end_reg"]
                f.write("        .db 0x%02X,0x%02X,0x%02X,0x%02X   ; %2d bank %2d phrase 0x%02X %.3fs\n" % (
                    s & 255, s >> 8, t & 255, t >> 8, e["index"], e["bank"], e["phrase"], e["duration"]))

    cols = ["index", "bank", "phrase", "oki_start", "oki_stop", "phys_start", "phys_stop", "nsamp_oki",
            "nsamp_ym", "duration", "vrom_start", "vrom_end", "start_reg", "end_reg", "rms_err", "same_as"]
    with open(f_csv, "w") as f:
        f.write(",".join(cols) + "\n")
        for e in entries:
            row = []
            for c in cols:
                v = e.get(c, "")
                if isinstance(v, float):
                    v = "%.5f" % v
                elif c in ("oki_start", "oki_stop", "phys_start", "phys_stop", "vrom_start", "vrom_end") and v != "":
                    v = "0x%X" % v
                row.append(str(v))
            f.write(",".join(row) + "\n")
    with open(os.path.splitext(f_csv)[0] + ".json", "w") as f:
        json.dump({"oki_rate": OKI_RATE, "adpcma_rate": ADPCMA_RATE, "vrom_size": len(vrom),
                   "entries": entries}, f, indent=1)

    mean_err = sum(errors) / len(errors) if errors else 0.0
    print("V-ROM: %d bytes (0x%X), %d unique phrases, %d table entries used, mean RMS rel. error %.4f, max %.4f" % (
        len(vrom), len(vrom), len(by_phys), sum(1 for e in entries if not e["empty"]), mean_err, max(errors) if errors else 0))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
