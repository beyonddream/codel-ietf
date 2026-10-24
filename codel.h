/* codel.h — CoDel active queue management.
 *
 * An exact port of the pseudocode in Section 4 of
 * draft-nichols-tsvwg-codel-01 (Nichols & Jacobson, February 2013),
 * "Controlled Delay Active Queue Management", the Internet-Draft
 * that became RFC 8289. State names, constants and control flow
 * follow the draft; only the language changed.
 *
 * Time is in milliseconds (double). Packet sizes are in bytes.
 * first_above_time is a deadline (0 means "below target"), exactly
 * as in the draft: it is set to now + interval when the queue first
 * goes above target, and ok_to_drop becomes true once now reaches it.
 */

#ifndef CODEL_H
#define CODEL_H

#include <stddef.h>

typedef struct codel_packet {
    double tstamp; /* enqueue time, ms */
    double size;   /* bytes */
    struct codel_packet *next;
} codel_packet_t;

typedef struct {
    double target;    /* acceptable standing-queue delay, ms (draft: 5) */
    double interval;  /* window the standing queue is judged over, ms (draft: 100) */
    double maxpacket; /* bytes (draft default 512; "should use interface MTU") */

    /* Per-queue state (draft section 4.2). */
    double first_above_time; /* deadline; 0 if below target */
    double drop_next;        /* time to drop next packet */
    unsigned int count;      /* packets dropped since entering drop state */
    int dropping;            /* 1 if in drop state */

    codel_packet_t *head, *tail;
    double bytes; /* bytes currently queued */
    size_t len;   /* packets currently queued */

    unsigned long drops; /* packets dropped by CoDel */
} codel_queue_t;

void codel_init(codel_queue_t *q, double target, double interval,
                double maxpacket);
void codel_enqueue(codel_queue_t *q, double now, double size);
/* Returns the delivered packet (caller frees it), or NULL if the queue
 * is empty or the dequeued packet was dropped entering/leaving the
 * dropping state. All CoDel decisions happen here, on dequeue. */
codel_packet_t *codel_dequeue(codel_queue_t *q, double now);
double codel_control_law(codel_queue_t *q, double t);

#endif /* CODEL_H */
