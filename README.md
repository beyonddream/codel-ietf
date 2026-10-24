# git init -b main

Exact C and Python ports of the IETF CoDel pseudocode, with
simulations showing standing-queue control under overload and bursts.

**Description:** CoDel (Controlled Delay) is the parameterless active
queue management algorithm by Nichols & Jacobson. This repo ports its
pseudocode exactly — same state, same constants, same control flow —
in both C and Python, and runs the same two simulations in both
languages so the implementations can be checked against each other.

## Source

The pseudocode in Section 4 ("Annotated Pseudo-code for CoDel") of
`draft-nichols-tsvwg-codel-01` (Nichols & Jacobson, February 2013),
the Internet-Draft that became RFC 8289:

- https://dataTracker.ietf.org/doc/html/draft-nichols-tsvwg-codel-01

Constants are the draft's: target 5 ms, interval 100 ms, maxpacket
512 bytes (the draft notes this "should use interface MTU").

## Fidelity notes

Two places where the port follows the draft's intent rather than a
literal reading of its HTML rendering:

- The draft's HTML rendering drops the `!` in the two dropping-state
  conditions of `deque()` (it shows `if (  r.ok_to_drop)` next to the
  comment "sojourn time below target - leave dropping state"). The
  comments state the intended polarity, and the ACM Queue appendix
  version of the same pseudocode has the `!` in place. The intended
  logic is implemented here.
- The draft's resume rule is used: on re-entering the dropping state
  within `8 * interval` of the last scheduled drop, `count` resumes at
  `count - 2` instead of restarting at 1. The ACM Queue appendix
  version uses a `1 * interval` window instead; the two published
  forms of the pseudocode differ here, and this repo follows the
  IETF draft.

Other details kept exactly: enqueue only timestamps the packet; all
decisions happen on dequeue; `first_above_time` is stored as a
deadline (`now + interval`), not an elapsed time; the control law
`interval / sqrt(count)` is applied to `drop_next` inside the
dropping-state while loop (so one dequeue call can drop several
packets); and no drop is considered while the queue holds at most
`maxpacket` bytes.

Note: the draft states that code components extracted from it must
include the license given with the code in its Section 4. Check that
license block before publishing this code under another license.

## Layout

```
codel.h      C queue: state, init/enqueue/dequeue/control_law
codel.c      C implementation (exact port)
codel.py     Python implementation (exact port, same semantics)
codel_sim.c  C simulations (main)
codel_sim.py Python simulations (same scenarios, tick for tick)
Makefile     builds codel_sim
```

## Build and run

Requirements: a C compiler (`cc`, `gcc`, or `clang`) and Python 3.
No libraries beyond libc/libm and the Python standard library.

```sh
# C version
make
./codel_sim

# Python version
python3 codel_sim.py
```

Both programs print identical output. `make clean` removes the binary.

## Simulations

Both scenarios use a 1-packet-per-ms server (one dequeue per 1 ms
tick) and 512-byte packets, so the `maxpacket` floor only bites when
at most one packet is queued. Each scenario runs CoDel and a plain
drop-tail FIFO baseline with identical arrivals. Stats are over
delivered packets only.

- **persistent-overload:** 1.05 arrivals/ms for 10,000 ms, as a
  deterministic 21-packets-per-20-ms pattern (1 packet every tick
  plus 1 extra every 20th tick). Reported at the end of the window,
  without draining, so the queue figure is the standing queue.
- **burst:** 4 arrivals/ms for 10 ms, then drained to empty.
  Reported after the drain, so every packet is accounted for.

## Results (C and Python agree exactly)

```
scenario: persistent-overload (1.05 arrivals/ms for 10000 ms, 1 packet/ms service)
  CoDel: arrivals=10500 delivered=10000 drops=495 queued=5 mean=9.0 p99=26.0 max=27.0
  FIFO:  arrivals=10500 delivered=10000 drops=0 queued=500 mean=238.5 p99=472.0 max=477.0
scenario: burst (4 arrivals/ms for 10 ms, 1 packet/ms service, drain to empty)
  CoDel: arrivals=40 delivered=40 drops=0 queued=0 mean=15.0 p99=30.0 max=30.0
  FIFO:  arrivals=40 delivered=40 drops=0 queued=0 mean=15.0 p99=30.0 max=30.0
```

Reading: under persistent mild overload, CoDel drops 495 packets —
essentially exactly the 500 excess arrivals — and holds the standing
queue at 5 packets with a 27 ms worst delivered delay, while FIFO's
queue grows to 500 and its worst delay to 477 ms. The short burst is
the "good queue": it clears before the 100 ms interval elapses, so
CoDel drops nothing and behaves exactly like FIFO.

One comparison with the earlier simplified port: its persistent
overload mean was 19.2 ms; the exact port's is 9.0 ms, with the same
tails (p99 26–27 ms, max 27 ms). The difference is the resume rule.
An instrumented run shows the queue entering the dropping state 52
times during the 10 s window — it oscillates in and out as drops bite
and the backlog rebuilds. Without the resume rule, every re-entry
restarts the drop schedule at one drop per 100 ms and the queue
swells each cycle; with it, the schedule resumes near its previous
rate and the standing queue stays tight.

## Using the queue in your own code

```c
codel_queue_t q;
codel_init(&q, 5.0, 100.0, 512.0);
codel_enqueue(&q, now_ms, packet_bytes);
codel_packet_t *p = codel_dequeue(&q, now_ms); /* NULL if none delivered */
if (p) { /* ... */ free(p); }
```

```python
from codel import CoDelQueue
q = CoDelQueue(target=5.0, interval=100.0, maxpacket=512.0)
q.enqueue(now_ms, packet_bytes)
packet, sojourn_ms = q.dequeue(now_ms)  # (None, None) if none delivered
```
