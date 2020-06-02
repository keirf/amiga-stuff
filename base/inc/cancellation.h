/*
 * cancellation.h
 * 
 * Asynchronously-cancellable function calls.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

/* Opaque to users of this interface. However the struct must be zeroed before 
 * first use. */
struct cancellation {
    void *sp;
};

static inline int cancellation_is_running(struct cancellation *c)
{
    return c->sp != NULL;
}

/* Execute (*fn)(arg) in a wrapped cancellable environment. */
int call_cancellable_fn(struct cancellation *c, int (*fn)(void *), void *arg);

/* From IRQ content: stop running fn() and immediately return -1. 
 * Returns 0 if the fn() either was not running, or was running but is now
 * succesfully cancelled. Returns -1 if the fn is running but cannot be 
 * cancelled by the caller. */
int cancel_call(struct cancellation *c, struct c_exception_frame *frame);

/*
 * Local variables:
 * mode: C
 * c-file-style: "Linux"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
