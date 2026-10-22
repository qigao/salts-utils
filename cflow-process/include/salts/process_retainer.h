#ifndef CFLOW_PROCESS_RETAINER_H
#define CFLOW_PROCESS_RETAINER_H

#include <salts/process.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Explicit bounded recovery owner for a closing Process whose native I/O
 * has not quiesced. The caller keeps this owner alive until all adopted
 * processes have completed and been destroyed.
 *
 * No global queue, detached worker, automatic retry thread or fallback
 * owner is created. One caller drives the retainer; reserve/adopt/run/destroy
 * are NOT concurrent operations.
 */
typedef struct cflow_process_retainer {
  void *impl;
} cflow_process_retainer;

typedef struct cflow_process_retainer_token {
  size_t index;
  uint64_t generation;
} cflow_process_retainer_token;

/*
 * Called exactly once, only after the adopted Process has released all native
 * I/O and its Process handle has become null following destroy(). The payload
 * must own every borrowed I/O buffer and the completion_user callback context,
 * and this callback must not throw or reenter the retainer.
 */
typedef void (*cflow_process_retainer_release_fn)(void *payload);

/* Fixed capacity allocated at initialization. */
int cflow_process_retainer_init(cflow_process_retainer *owner, size_t capacity);

/*
 * Reserve a recovery slot BEFORE beginning the process. Saturation fails
 * closed with SALTS_ENOBUFS; callers may not start a new process without
 * an available reservation when bounded cancellation is required.
 */
int cflow_process_retainer_reserve(cflow_process_retainer *owner,
                                  cflow_process_retainer_token *out);

/* Return an unused reservation. Does not touch any native Process. */
int cflow_process_retainer_release(cflow_process_retainer *owner,
                                  cflow_process_retainer_token token);

/*
 * Move a closing Process plus its stable callback/buffer payload into the
 * reserved slot. On success source->impl is nulled. No native I/O is freed.
 * Must be called after producers and the Process driver have stopped.
 * On failure all ownership remains with the caller.
 */
int cflow_process_retainer_adopt(
    cflow_process_retainer *owner, cflow_process_retainer_token token,
    cflow_process *source, void *payload,
    cflow_process_retainer_release_fn release);

/*
 * Drive one bounded phase per adopted Process (max_steps per Process),
 * reaping only when quiescent and releasing payload after Process destroy.
 * Returns SALTS_EBUSY while reservations or adopted processes remain, or
 * the first actual close/drive/destroy error; never force-frees a live lease.
 * out_pending includes both reservations and adopted processes.
 */
int cflow_process_retainer_run_ready(cflow_process_retainer *owner,
                                     size_t max_steps,
                                     size_t *out_reaped,
                                     size_t *out_pending);

/*
 * Destruction is refused with SALTS_EBUSY while anything is reserved or
 * adopted. The caller MUST keep owner storage valid if EBUSY is returned.
 */
int cflow_process_retainer_destroy(cflow_process_retainer *owner);

#ifdef __cplusplus
}
#endif

#endif /* CFLOW_PROCESS_RETAINER_H */
