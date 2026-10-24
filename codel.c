/* codel.c — CoDel active queue management.
 *
 * An exact port of the pseudocode in Section 4 of
 * draft-nichols-tsvwg-codel-01 (Nichols & Jacobson, February 2013),
 * the Internet-Draft that became RFC 8289.
 *
 * Two notes on the source text:
 *  - The draft's HTML rendering drops the '!' in the two dropping-state
 *    conditions of deque() (it shows "if (  r.ok_to_drop)" next to the
 *    comment "sojourn time below target - leave dropping state").
 *    The comments state the intended polarity, and the ACM Queue
 *    appendix version of the same pseudocode has the '!' in place;
 *    the intended logic is what is implemented here.
 *  - The draft's resume rule (count resumes at count - 2 when
 *    re-entering within 8 * interval of the last scheduled drop) is
 *    used here, not the ACM Queue appendix's 1 * interval variant.
 */

#include "codel.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
    codel_packet_t *p;
    int ok_to_drop;
} dodeque_result_t;

void codel_init(codel_queue_t *q, double target, double interval,
                double maxpacket) {
    q->target = target;
    q->interval = interval;
    q->maxpacket = maxpacket;
    q->first_above_time = 0.0;
    q->drop_next = 0.0;
    q->count = 0;
    q->dropping = 0;
    q->head = q->tail = NULL;
    q->bytes = 0.0;
    q->len = 0;
    q->drops = 0;
}

void codel_enqueue(codel_queue_t *q, double now, double size) {
    /* The only CoDel addition to enque is stamping the packet. */
    codel_packet_t *p = malloc(sizeof *p);
    if (!p) {
        fprintf(stderr, "codel: out of memory\n");
        exit(1);
    }
    p->tstamp = now;
    p->size = size;
    p->next = NULL;
    if (q->tail)
        q->tail->next = p;
    else
        q->head = p;
    q->tail = p;
    q->len++;
    q->bytes += size;
}

double codel_control_law(codel_queue_t *q, double t) {
    /* Called only with count >= 1 (see codel_dequeue). */
    return t + q->interval / sqrt((double)q->count);
}

/* Dequeues one packet and reports whether it is OK to drop: the
 * sojourn time has been above target for at least interval. */
static dodeque_result_t dodeque(codel_queue_t *q, double now) {
    dodeque_result_t r;
    r.p = NULL;
    r.ok_to_drop = 0;

    codel_packet_t *p = q->head;
    if (!p) {
        /* Queue empty: we can't be above target. */
        q->first_above_time = 0.0;
        return r;
    }
    q->head = p->next;
    if (!q->head)
        q->tail = NULL;
    q->len--;
    q->bytes -= p->size;

    double sojourn = now - p->tstamp;
    if (sojourn < q->target || q->bytes <= q->maxpacket) {
        /* Went below target (or less than an MTU left queued). */
        q->first_above_time = 0.0;
    } else {
        if (q->first_above_time == 0.0) {
            /* Just went above from below: ok to drop if still above
             * at first_above_time, one interval out. */
            q->first_above_time = now + q->interval;
        } else if (now >= q->first_above_time) {
            r.ok_to_drop = 1;
        }
    }
    r.p = p;
    return r;
}

codel_packet_t *codel_dequeue(codel_queue_t *q, double now) {
    dodeque_result_t r = dodeque(q, now);

    if (q->dropping) {
        if (!r.ok_to_drop) {
            /* Sojourn time below target: leave dropping state. */
            q->dropping = 0;
        }
        /* Time for the next drop(s). A large backlog might make the
         * next drop due now, hence the while loop. */
        while (now >= q->drop_next && q->dropping) {
            free(r.p);
            q->drops++;
            r = dodeque(q, now);
            if (!r.ok_to_drop) {
                /* Leave dropping state. */
                q->dropping = 0;
            } else {
                q->count++;
                /* Schedule the next drop from the previous schedule. */
                q->drop_next = codel_control_law(q, q->drop_next);
            }
        }
    } else if (r.ok_to_drop) {
        /* Sojourn has been above target for interval: enter dropping
         * state, dropping this packet. */
        free(r.p);
        q->drops++;
        r = dodeque(q, now);
        q->dropping = 1;
        /* If we re-entered close to the last drop cycle, resume near
         * the previous drop rate instead of starting from scratch. */
        if (q->count > 2 && now - q->drop_next < 8 * q->interval)
            q->count -= 2;
        else
            q->count = 1;
        q->drop_next = codel_control_law(q, now);
    }
    return r.p;
}
