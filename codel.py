"""codel.py — CoDel active queue management.

An exact port of the pseudocode in Section 4 of
draft-nichols-tsvwg-codel-01 (Nichols & Jacobson, February 2013),
the Internet-Draft that became RFC 8289. State names, constants and
control flow follow the draft; only the language changed.

Time is in milliseconds (float). Packet sizes are in bytes.
first_above_time is a deadline (0.0 means "below target"), exactly as
in the draft: it is set to now + interval when the queue first goes
above target, and ok_to_drop becomes true once now reaches it.

Two notes on the source text:
 - The draft's HTML rendering drops the '!' in the two dropping-state
   conditions of deque() (it shows "if (  r.ok_to_drop)" next to the
   comment "sojourn time below target - leave dropping state").
   The comments state the intended polarity, and the ACM Queue
   appendix version of the same pseudocode has the '!' in place;
   the intended logic is what is implemented here.
 - The draft's resume rule (count resumes at count - 2 when
   re-entering within 8 * interval of the last scheduled drop) is
   used here, not the ACM Queue appendix's 1 * interval variant.
"""

import math
from collections import deque


class CoDelQueue:
    """A single FIFO queue with CoDel active queue management."""

    def __init__(self, target=5.0, interval=100.0, maxpacket=512.0):
        self.target = target      # acceptable standing-queue delay, ms
        self.interval = interval  # window the standing queue is judged over, ms
        self.maxpacket = maxpacket  # bytes (draft default 512; "should use interface MTU")

        # Per-queue state (draft section 4.2).
        self.first_above_time = 0.0  # deadline; 0.0 if below target
        self.drop_next = 0.0         # time to drop next packet
        self.count = 0               # packets dropped since entering drop state
        self.dropping = False        # in drop state?

        self._queue = deque()  # items: (enqueue_time_ms, size_bytes)
        self.bytes = 0.0       # bytes currently queued
        self.drops = 0         # packets dropped by CoDel

    def __len__(self):
        return len(self._queue)

    def enqueue(self, now, size):
        """Stamp the packet with arrival time `now` (ms)."""
        # The only CoDel addition to enque is stamping the packet.
        self._queue.append((now, size))
        self.bytes += size

    def control_law(self, t):
        # Called only with count >= 1 (see dequeue).
        return t + self.interval / math.sqrt(self.count)

    def _dodeque(self, now):
        """Dequeue one packet; report whether it is OK to drop it.

        Returns ((tstamp, size), ok_to_drop), or (None, False) when empty.
        ok_to_drop means the sojourn time has been above target for at
        least interval.
        """
        if not self._queue:
            # Queue empty: we can't be above target.
            self.first_above_time = 0.0
            return None, False
        tstamp, size = self._queue.popleft()
        self.bytes -= size
        sojourn = now - tstamp
        ok_to_drop = False
        if sojourn < self.target or self.bytes <= self.maxpacket:
            # Went below target (or less than an MTU left queued).
            self.first_above_time = 0.0
        else:
            if self.first_above_time == 0.0:
                # Just went above from below: ok to drop if still above
                # at first_above_time, one interval out.
                self.first_above_time = now + self.interval
            elif now >= self.first_above_time:
                ok_to_drop = True
        return (tstamp, size), ok_to_drop

    def dequeue(self, now):
        """Return (packet, sojourn_ms), or (None, None).

        All CoDel decisions happen here, on dequeue. A packet CoDel
        drops is discarded and dequeue moves on to the next packet, so
        one call can drop several packets (the draft's while loop).
        """
        packet, ok_to_drop = self._dodeque(now)
        if self.dropping:
            if not ok_to_drop:
                # Sojourn time below target: leave dropping state.
                self.dropping = False
            # Time for the next drop(s). A large backlog might make the
            # next drop due now, hence the while loop.
            while now >= self.drop_next and self.dropping:
                self.drops += 1
                packet, ok_to_drop = self._dodeque(now)
                if not ok_to_drop:
                    # Leave dropping state.
                    self.dropping = False
                else:
                    self.count += 1
                    # Schedule the next drop from the previous schedule.
                    self.drop_next = self.control_law(self.drop_next)
        elif ok_to_drop:
            # Sojourn has been above target for interval: enter dropping
            # state, dropping this packet.
            self.drops += 1
            packet, ok_to_drop = self._dodeque(now)
            self.dropping = True
            # If we re-entered close to the last drop cycle, resume near
            # the previous drop rate instead of starting from scratch.
            if self.count > 2 and now - self.drop_next < 8 * self.interval:
                self.count -= 2
            else:
                self.count = 1
            self.drop_next = self.control_law(now)
        if packet is None:
            return None, None
        tstamp, _size = packet
        return packet, now - tstamp
