/* codel_sim.c — simulations for the exact IETF CoDel port.
 *
 * Two scenarios, both with a 1-packet-per-ms server (one dequeue per
 * 1 ms tick) and 512-byte packets (so the draft's maxpacket floor of
 * 512 bytes only bites when at most one packet is queued):
 *
 *   persistent-overload: 1.05 arrivals/ms for 10,000 ms, as a
 *       deterministic 21-packets-per-20-ms pattern (1 packet every tick
 *       plus 1 extra every 20th tick). Reported at the end of the window,
 *       without draining, so the queue figure is the standing queue.
 *   burst: 4 arrivals/ms for 10 ms, then drained to empty. Reported
 *       after the drain, so every packet is accounted for.
 *
 * Each scenario runs CoDel and a plain drop-tail FIFO baseline with
 * identical arrivals. Stats are over delivered packets only.
 *
 * This driver mirrors codel_sim.py tick for tick, so the two programs
 * print identical results.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "codel.h"

#define TARGET_MS 5.0
#define INTERVAL_MS 100.0
#define MAXPACKET_BYTES 512.0
#define PACKET_BYTES 512.0

/* --- Minimal queue-operation abstraction so both scenarios share one
 * loop per queue type. --- */

typedef struct {
    void *q;
    void (*enqueue)(void *q, double now, double size);
    /* Returns 1 if a packet was delivered, setting *sojourn. */
    int (*dequeue)(void *q, double now, double *sojourn);
    unsigned long (*drops)(void *q);
    size_t (*len)(void *q);
} queue_ops_t;

/* CoDel wrappers. */
static void codel_enqueue_wrap(void *q, double now, double size) {
    codel_enqueue((codel_queue_t *)q, now, size);
}

static int codel_dequeue_wrap(void *q, double now, double *sojourn) {
    codel_packet_t *p = codel_dequeue((codel_queue_t *)q, now);
    if (!p)
        return 0;
    *sojourn = now - p->tstamp;
    free(p);
    return 1;
}

static unsigned long codel_drops_wrap(void *q) {
    return ((codel_queue_t *)q)->drops;
}

static size_t codel_len_wrap(void *q) {
    return ((codel_queue_t *)q)->len;
}

/* Plain drop-tail FIFO baseline. */
typedef struct fnode {
    double tstamp;
    struct fnode *next;
} fnode_t;

typedef struct {
    fnode_t *head, *tail;
    size_t len;
} fifo_t;

static void fifo_enqueue_wrap(void *q, double now, double size) {
    (void)size;
    fifo_t *f = (fifo_t *)q;
    fnode_t *n = malloc(sizeof *n);
    if (!n) {
        fprintf(stderr, "codel_sim: out of memory\n");
        exit(1);
    }
    n->tstamp = now;
    n->next = NULL;
    if (f->tail)
        f->tail->next = n;
    else
        f->head = n;
    f->tail = n;
    f->len++;
}

static int fifo_dequeue_wrap(void *q, double now, double *sojourn) {
    fifo_t *f = (fifo_t *)q;
    fnode_t *n = f->head;
    if (!n)
        return 0;
    f->head = n->next;
    if (!f->head)
        f->tail = NULL;
    f->len--;
    *sojourn = now - n->tstamp;
    free(n);
    return 1;
}

static unsigned long fifo_drops_wrap(void *q) {
    (void)q;
    return 0;
}

static size_t fifo_len_wrap(void *q) {
    return ((fifo_t *)q)->len;
}

/* --- Stats --- */

static int cmp_double(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

static void report(const char *name, long arrivals, double *soj, size_t n,
                   unsigned long drops, size_t queued) {
    double mean = 0.0, p99 = 0.0, maxv = 0.0;
    if (n) {
        qsort(soj, n, sizeof(double), cmp_double);
        double sum = 0.0;
        for (size_t i = 0; i < n; i++)
            sum += soj[i];
        mean = sum / (double)n;
        p99 = soj[(size_t)ceil(0.99 * (double)n) - 1];
        maxv = soj[n - 1];
    }
    printf("  %s: arrivals=%ld delivered=%zu drops=%lu queued=%zu "
           "mean=%.1f p99=%.1f max=%.1f\n",
           name, arrivals, n, drops, queued, mean, p99, maxv);
}

/* --- Scenarios --- */

static void run_persistent(void) {
    printf("scenario: persistent-overload "
           "(1.05 arrivals/ms for 10000 ms, 1 packet/ms service)\n");
    double *soj = malloc(20000 * sizeof(double));
    if (!soj) {
        fprintf(stderr, "codel_sim: out of memory\n");
        exit(1);
    }

    codel_queue_t cq;
    codel_init(&cq, TARGET_MS, INTERVAL_MS, MAXPACKET_BYTES);
    queue_ops_t codel_ops = { &cq, codel_enqueue_wrap, codel_dequeue_wrap,
                              codel_drops_wrap, codel_len_wrap };
    fifo_t fq = { NULL, NULL, 0 };
    queue_ops_t fifo_ops = { &fq, fifo_enqueue_wrap, fifo_dequeue_wrap,
                             fifo_drops_wrap, fifo_len_wrap };
    queue_ops_t *ops_list[2] = { &codel_ops, &fifo_ops };
    const char *names[2] = { "CoDel", "FIFO" };

    for (int k = 0; k < 2; k++) {
        queue_ops_t *ops = ops_list[k];
        long arrivals = 0;
        size_t ndel = 0;
        for (long t = 0; t < 10000; t++) {
            ops->enqueue(ops->q, (double)t, PACKET_BYTES);
            arrivals++;
            if (t % 20 == 0) {
                ops->enqueue(ops->q, (double)t, PACKET_BYTES);
                arrivals++;
            }
            double sojourn;
            if (ops->dequeue(ops->q, (double)t, &sojourn))
                soj[ndel++] = sojourn;
        }
        report(names[k], arrivals, soj, ndel, ops->drops(ops->q),
               ops->len(ops->q));
    }
    free(soj);
}

static void run_burst(void) {
    printf("scenario: burst "
           "(4 arrivals/ms for 10 ms, 1 packet/ms service, drain to empty)\n");
    double *soj = malloc(20000 * sizeof(double));
    if (!soj) {
        fprintf(stderr, "codel_sim: out of memory\n");
        exit(1);
    }

    codel_queue_t cq;
    codel_init(&cq, TARGET_MS, INTERVAL_MS, MAXPACKET_BYTES);
    queue_ops_t codel_ops = { &cq, codel_enqueue_wrap, codel_dequeue_wrap,
                              codel_drops_wrap, codel_len_wrap };
    fifo_t fq = { NULL, NULL, 0 };
    queue_ops_t fifo_ops = { &fq, fifo_enqueue_wrap, fifo_dequeue_wrap,
                             fifo_drops_wrap, fifo_len_wrap };
    queue_ops_t *ops_list[2] = { &codel_ops, &fifo_ops };
    const char *names[2] = { "CoDel", "FIFO" };

    for (int k = 0; k < 2; k++) {
        queue_ops_t *ops = ops_list[k];
        long arrivals = 0;
        size_t ndel = 0;
        long t = 0;
        for (;;) {
            if (t < 10) {
                for (int i = 0; i < 4; i++) {
                    ops->enqueue(ops->q, (double)t, PACKET_BYTES);
                    arrivals++;
                }
            }
            double sojourn;
            if (ops->dequeue(ops->q, (double)t, &sojourn))
                soj[ndel++] = sojourn;
            t++;
            if (t >= 10 && ops->len(ops->q) == 0)
                break;
            if (t > 100000)
                break;
        }
        report(names[k], arrivals, soj, ndel, ops->drops(ops->q),
               ops->len(ops->q));
    }
    free(soj);
}

int main(void) {
    run_persistent();
    run_burst();
    return 0;
}
