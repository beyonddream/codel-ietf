"""codel_sim.py — simulations for the exact IETF CoDel port.

Two scenarios, both with a 1-packet-per-ms server (one dequeue per
1 ms tick) and 512-byte packets (so the draft's maxpacket floor of
512 bytes only bites when at most one packet is queued):

  persistent-overload: 1.05 arrivals/ms for 10,000 ms, as a
      deterministic 21-packets-per-20-ms pattern (1 packet every tick
      plus 1 extra every 20th tick). Reported at the end of the window,
      without draining, so the queue figure is the standing queue.
  burst: 4 arrivals/ms for 10 ms, then drained to empty. Reported
      after the drain, so every packet is accounted for.

Each scenario runs CoDel and a plain drop-tail FIFO baseline with
identical arrivals. Stats are over delivered packets only.
"""

import math
from collections import deque

from codel import CoDelQueue

TARGET = 5.0
INTERVAL = 100.0
MAXPACKET = 512.0
PACKET_BYTES = 512.0


class FifoQueue:
    """Plain drop-tail FIFO, for the baseline. Same dequeue shape."""

    def __init__(self):
        self._queue = deque()
        self.drops = 0

    def __len__(self):
        return len(self._queue)

    def enqueue(self, now, size):
        self._queue.append(now)

    def dequeue(self, now):
        if not self._queue:
            return None, None
        tstamp = self._queue.popleft()
        return tstamp, now - tstamp


def report(name, arrivals, sojourns, drops, queued):
    s = sorted(sojourns)
    n = len(s)
    mean = sum(s) / n if n else 0.0
    p99 = s[math.ceil(0.99 * n) - 1] if n else 0.0
    maxv = s[-1] if n else 0.0
    print(f"  {name}: arrivals={arrivals} delivered={n} drops={drops} "
          f"queued={queued} mean={mean:.1f} p99={p99:.1f} max={maxv:.1f}")


def run_persistent():
    print("scenario: persistent-overload "
          "(1.05 arrivals/ms for 10000 ms, 1 packet/ms service)")
    for name, queue in (("CoDel", CoDelQueue(TARGET, INTERVAL, MAXPACKET)),
                        ("FIFO", FifoQueue())):
        arrivals = 0
        sojourns = []
        for t in range(10000):
            queue.enqueue(float(t), PACKET_BYTES)
            arrivals += 1
            if t % 20 == 0:
                queue.enqueue(float(t), PACKET_BYTES)
                arrivals += 1
            packet, sojourn = queue.dequeue(float(t))
            if packet is not None:
                sojourns.append(sojourn)
        report(name, arrivals, sojourns, queue.drops, len(queue))


def run_burst():
    print("scenario: burst "
          "(4 arrivals/ms for 10 ms, 1 packet/ms service, drain to empty)")
    for name, queue in (("CoDel", CoDelQueue(TARGET, INTERVAL, MAXPACKET)),
                        ("FIFO", FifoQueue())):
        arrivals = 0
        sojourns = []
        t = 0
        while True:
            if t < 10:
                for _ in range(4):
                    queue.enqueue(float(t), PACKET_BYTES)
                    arrivals += 1
            packet, sojourn = queue.dequeue(float(t))
            if packet is not None:
                sojourns.append(sojourn)
            t += 1
            if t >= 10 and len(queue) == 0:
                break
            if t > 100000:
                break
        report(name, arrivals, sojourns, queue.drops, len(queue))


if __name__ == "__main__":
    run_persistent()
    run_burst()
