.. SPDX-License-Identifier: CC-BY-SA-4.0

==========
DFC engine
==========

A hardware *dynamic frequency change* (DFC) engine moves the running controller
between frequency points without re-initializing it. The firmware loads a
command table once and then kicks one frequency change at a time. The table
format, the command flows and the kick protocol are in :dc:`dfc-engine`; the
firmware side is :doc:`../firmware/dfc`.

The table window
================

The table is written through a separate window (``0xf0000000`` in the image),
one entry at a time, as a (DATA, ADDR, INDEX) triple where writing INDEX commits
the entry. The window may or may not alias the MC; the firmware works either way
and logs which it is. The table has an entry flow, an exit flow, a snapshot of
the live MC state (globals plus the four banks) and a tail, delimited by header
and end flags, followed by three PHY marker triplets (:dc:`dfc-engine`,
:doc:`../firmware/dfc`).

Building the snapshot captures the banked MC registers by temporarily selecting
each bank on the live MC and reading the offsets back, so it must run with the
DRAM idle.

The kick
========

A DFC is kicked by masking the DCLK frequency-change IRQ, writing a pre-command
word, selecting the target clock word and ``HW_SLP``, then writing ``CC_AP = 1
<< 22`` (a whole-word write) and waiting for bit 22 to clear. The engine is
refused while ranks are in self-refresh or while a previous kick is still stuck
(bit 22 held), which only an MC reset clears (:doc:`../firmware/dfc`).

On success the engine switches the bank, updates ``CTL_FREQ``/``PI_CFG``/STATUS
and re-sends MR14 from the target bank. A kick with an incomplete table parks the
ranks and leaves ``CTL_FREQ`` unchanged — the firmware detects this as a timeout
and recovers the DRAM from self-refresh before stopping the boot.

16 GiB variant
==============

For 16 GiB parts an extra MR21 write is inserted into both command flows and the
following entries shift up by one index (:dc:`dfc-engine`,
:doc:`../firmware/dfc`).
