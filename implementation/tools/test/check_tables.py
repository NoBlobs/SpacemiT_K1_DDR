#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Thalles Campagnani
"""Diff the silicon tables printed by dump_tables against cleanroom/docs.

usage: check_tables.py <dump_tables binary> <docs dir>
"""
import re
import subprocess
import sys
from collections import Counter
from pathlib import Path

SEP = re.compile(r'^=+( +=+)+\s*$')
NUM = re.compile(r'0x[0-9a-fA-F]+|\d+')


def first_num(s):
    m = NUM.search(s)
    if not m:
        sys.exit(f'no number in {s!r}')
    return int(m.group(0), 0)


def simple_tables(text):
    """reST simple tables as (header cells, body rows); continuation lines merged."""
    lines = text.splitlines()
    out = []
    i = 0
    while i < len(lines):
        if not SEP.match(lines[i]):
            i += 1
            continue
        starts = [m.start() for m in re.finditer(r'=+', lines[i])]
        ends = starts[1:] + [None]

        def cells(line):
            return [line[s:e].strip() for s, e in zip(starts, ends)]

        j = i + 1
        head = []
        while j < len(lines) and not SEP.match(lines[j]):
            head.append(cells(lines[j]))
            j += 1
        j += 1
        rows = []
        while j < len(lines) and not SEP.match(lines[j]):
            if lines[j].strip():
                c = cells(lines[j])
                if c[0] == '' and rows:
                    rows[-1] = [(a + ' ' + b).strip() for a, b in zip(rows[-1], c)]
                else:
                    rows.append(c)
            j += 1
        out.append((head[0] if head else [], rows))
        i = j + 1
    return out


def table(doc, name, *head):
    want = [h.lower() for h in head]
    for h, rows in simple_tables(doc):
        if [c.lower() for c in h[:len(want)]] == want:
            return rows
    sys.exit(f'{name}: table {head} not found')


def spec_only(text):
    # implementer measurements are paragraphs led by an *Medido / *Nota
    return re.sub(r'^\*(Medido|Nota) .*?(?=\n\s*\n)', '', text,
                  flags=re.M | re.S)


def doc_facts(docs):
    d = {n: spec_only((docs / f).read_text(encoding='utf-8')) for n, f in {
        25: '25-pmu.rst', 26: '26-device-init.rst', 27: '27-phy-init.rst',
        28: '28-mr-init.rst', 29: '29-mapa.rst', 30: '30-dfc.rst',
        31: '31-configuracoes.rst'}.items()}
    f = set()

    for r in table(d[26], '26', 'offset', 'valor'):
        f.add(f'global {int(r[0].lstrip("+"), 0):#x} {int(r[1], 0):#x}')
    for r in table(d[26], '26', 'fp', 'CTL_FREQ'):
        f.add(f'freq {int(r[0])} {int(r[1], 0):#x}')
    for r in table(d[26], '26', 'offset', 'fp3 2133'):
        off = int(r[0].lstrip('+'), 0)
        if r[1].startswith('IO'):
            f.add(f'timing {off:#x} io')
        else:
            f.add(f'timing {off:#x} ' +
                  ' '.join(f'{int(c.split()[0], 0):#x}' for c in r[1:5]))
    for off, v in re.findall(r'ddr\+0x(38[048]) = (0x[0-9a-f]+)', d[26]):
        f.add(f'refresh {int(off, 16):#x} {int(v, 0):#x}')
    for fp, v in re.findall(r'^\s+fp(\d): (0x[0-9a-f]+)', d[26], re.M):
        f.add(f'wdqs {fp} {int(v, 0):#x}')

    for r in table(d[31], '31', 'fp', 'CTL_FREQ', 'ddr+0x13e4', 'ddr+0x3b4'):
        fp = int(r[0])
        f.add(f'freq {fp} {int(r[1], 0):#x}')
        f.add(f'overlay {fp} 0x13e4 {int(r[2], 0):#x}')
        f.add(f'overlay {fp} 0x3b4 {int(r[3], 0):#x}')

    for r in table(d[25], '25', 'fp', 'clksel'):
        if int(r[0]) < 4:
            f.add(f'clksel {int(r[0])} {int(r[1], 0):#x}')
    for fp, v in re.findall(r'fp(\d)\s+``(0x[0-9a-f]+)``', d[25]):
        f.add(f'hwslp {fp} {int(v, 0):#x}')
    for v in re.findall(r'^\s+(0x[0-9a-f]+)\s+# default', d[25], re.M):
        f.add(f'swfc_slp {int(v, 0):#x}')
    for v in re.findall(r'\+0xb0 = (0x[0-9a-f]+)``', d[25]):
        f.add(f'swfc_slp {int(v, 0):#x}')
    pll = re.findall(r'PMU\+0x(39c|3a0|3a8|3ac) = (0x[0-9a-f]+)', d[25])
    # in the full words the VCO byte sits in [23:16]; doc 25 prose says [15:8]
    for (o1, v1), (o2, v2) in zip(pll[::2], pll[1::2]):
        vco = int(v1, 0)
        b16, b8 = (vco >> 16) & 0xff, (vco >> 8) & 0xff
        # 2666: complete word, VCO in [23:16]. 2400: leftover RMW, VCO in [15:8].
        name = {('39c', 0x3c): '2666'}.get((o1, b16))
        if name is None and o1 == '39c' and b8 == 0x3b:
            name = '2400'
        if name is None:
            name = 'pll2' if o1 == '3a8' else f'?{o1}'
        f.add(f'pll {name} {vco:#x} {int(v2, 0):#x}')

    enc_line = next(l for l in d[26].splitlines() if '1=240' in l)
    ohm_enc = {int(o): int(e) for e, o in re.findall(r'(\d)=(\d+)', enc_line)}
    io_names = {'CA Vref': 'ca_vref', 'Tx Vref': 'tx_vref', 'Rx SoC': 'soc_odt',
                'ODT Tx': 'tx_odt', 'Rx drv': 'rx_drv', 'Tx p/n': 'tx_pn',
                'Rx Vref': 'rx_vref', 'VOH': 'voh'}
    for r in table(d[27], '27', 'param', 'sítio'):
        key = io_names.get(r[0])
        if not key:
            continue
        if 'nos dois' in r[2]:
            a = b = first_num(r[2])
        else:
            a, b = (first_num(x) for x in r[2].split(' / ', 1))
        f.add(f'io lpddr4x {key} {a:#x}')
        f.add(f'io lpddr4 {key} {b:#x}')
    f.add(f'io lpddr4x rx_odt {int(re.search(r"LPDDR4X R_\d+=(\d)", d[27]).group(1)):#x}')
    f.add(f'io lpddr4 rx_odt {ohm_enc[int(re.search(r"Rx ODT (\d+)", d[31]).group(1))]:#x}')

    seqs = re.findall(r'^\s+(MR\d+(?:, MR\d+)+)\s*$', d[28], re.M)
    for n, s in enumerate(seqs, 1):
        f.add(f'mr_init{n} ' + ' '.join(x[2:] for x in s.split(', ')))
    for doc in (d[28], d[30], d[31]):
        for v in re.findall(r'0x1302009[0-9a-f]', doc):
            f.add(f'mr21 {int(v, 0):#x}')

    entries = {}
    after = {}
    last = None
    for line in d[30].splitlines():
        m = re.match(r'^\s+(0x[0-9a-f]{8})\s+(0x[0-9a-f]{8})\s+(0x[0-9a-f]+|\d+)\b', line)
        if m:
            data, addr, idx = (int(x, 0) for x in m.groups())
            entries[idx] = (data, addr)
            last = idx
        elif 'se 16 GiB' in line and line.strip().startswith('#'):
            after['flow' if last < 0x180 else 'exit'] = last
    for idx, (data, addr) in entries.items():
        f.add(f'{"flow" if idx < 0x180 else "exit"} {idx:#x} {data:#x} {addr:#x}')
    for name, a in after.items():
        end = a
        while end + 1 in entries:
            end += 1
        f.add(f'mr21_after {name} {a:#x} {end:#x}')

    def between(doc, a, b):
        return doc[doc.index(a):doc.index(b, doc.index(a))]

    def offsets(text):
        return [int(x, 0) for x in re.findall(r'0x[0-9a-f]{3,4}\b', text)]

    for i, o in enumerate(offsets(between(d[30], 'Globais (offsets', 'Timings ('))):
        f.add(f'snapg {i} {o:#x}')
    head = int(re.search(r'1 linha ``\+(0x[0-9a-f]+)``', d[30]).group(1), 0)
    timings = offsets(between(d[30], 'Timings (', 'Índice e flags'))
    for i, o in enumerate([head] + timings):
        f.add(f'snapb {i} {o:#x}')
    a, b = re.search(r'\(ddr\+0x44\) \| \(1<<(\d+)\) \| \(1<<(\d+)\)', d[30]).groups()
    f.add(f'snap_ctl0_bits {(1 << int(a)) | (1 << int(b)):#x}')
    first = re.search(r'ADDR = ``(0x[0-9a-f]+)`` \(bit 16, REQ_PMU\)', d[30])
    f.add(f'snap_first {int(first.group(1), 0):#x}')
    end = re.search(r'A linha (0x[0-9a-f]+): DATA = o CTL0.*?ADDR = ``(0x[0-9a-f]+)``',
                    d[30], re.S)
    f.add(f'snap_end {int(end.group(1), 0):#x} {int(end.group(2), 0):#x}')
    for r in table(d[30], '30', 'bits', 'nome'):
        if r[0].isdigit():
            f.add(f'tbl_flag {r[1]} {1 << int(r[0]):#x}')
    for r in table(d[30], '30', 'fp', 'vivo [31:28]', 'tabela [31:28]', 'idx'):
        f.add(f'snap_bank {r[0]} {int(r[2], 0):#x} {int(r[3], 0):#x}')
    for r in table(d[30], '30', 'idx', 'DATA', 'ADDR'):
        if r[1].startswith('0x'):
            f.add(f'snapt {int(r[0], 0):#x} {int(r[1], 0):#x} {int(r[2], 0):#x}')
    marks = re.findall(r'T\+0x40000\+0x10104 = (0x\w+)\s+'
                       r'T\+0x40000\+0x10108 = (0x\w+)\s+'
                       r'T\+0x40000\+0x10100 = (0x\w+)', d[30])
    for i, m in enumerate(marks):
        f.add(f'mark {i} ' + ' '.join(f'{int(x, 0):#x}' for x in m))
    f.add(f'dfc_pre {int(re.search(r"ddr\+0x148\s+= (0x[0-9a-f]+)", d[30]).group(1), 0):#x}')

    code_of = {}
    for r in table(d[29], '29', 'código', 'tamanho'):
        code, mb = int(r[0], 0), first_num(r[1])
        code_of[mb] = code
        f.add(f'size {code:#x} {mb}')

    rate_rows = table(d[30], '30', 'taxa', 'fp', 'notas')
    fp_of = {int(r[0]): (int(r[1]), r[2]) for r in rate_rows}
    default = next(fp for fp, notes in fp_of.values() if 'default' in notes)
    for rate, (fp, notes) in fp_of.items():
        f.add(f'rate lpddr4 {rate} {fp}')
        f.add(f'rate lpddr4x {rate} {default if "LPDDR4X recusa" in notes else fp}')
    others, like = re.search(r'((?:\d+,?\s+)+)e qualquer outro:\s+\*\*não\*\*\s+'
                             r'são fp de boot\. Trate como (\d+)', d[30]).groups()
    for rate in re.findall(r'\d+', others):
        for t in ('lpddr4', 'lpddr4x'):
            f.add(f'rate {t} {rate} {fp_of[int(like)][0]}')

    dens = {int(c): int(g) for c, g in re.findall(r'(\d+)=(\d+)Gb', d[29])}
    ch_mb = {int(g): int(mb) for g, mb in re.findall(r'(\d+)Gb→(\d+)', d[29])}
    row_of = {}
    for r in table(d[29], '29', 'io, densidade', 'ROW'):
        io, gbs, _ = r[0].split()
        for g in gbs.split('/'):
            row_of[(io, int(g))] = int(r[1])
    for io, mult in (('x16', 1), ('x8', 2)):
        for g in dens.values():
            area = ch_mb[g] * mult * 2 if g in ch_mb else None
            if area in code_of:
                f.add(f'geom {io} {g} {area} {code_of[area]:#x} {row_of[(io, g)]}')
            else:
                f.add(f'geom {io} {g} none')
    return f


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    out = subprocess.run([sys.argv[1]], capture_output=True, text=True, check=True)
    c = out.stdout.splitlines()
    dup = [l for l, n in Counter(c).items() if n > 1]
    c = set(c)
    docs = doc_facts(Path(sys.argv[2]))
    only_doc = sorted(docs - c)
    only_c = sorted(c - docs)
    kinds = Counter(l.split()[0] for l in docs & c)
    print('  ' + ', '.join(f'{k} {n}' for k, n in sorted(kinds.items())))
    for l in dup:
        print(f'  duplicate in C: {l}')
    for l in only_doc:
        print(f'  docs only: {l}')
    for l in only_c:
        print(f'  C only:    {l}')
    if only_doc or only_c or dup:
        sys.exit(f'check-tables: {len(only_doc)} docs-only, {len(only_c)} C-only')
    print(f'check-tables: {len(docs)} facts match')


if __name__ == '__main__':
    main()
