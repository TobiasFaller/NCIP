#ifndef _kitten_h_INCLUDED
#define _kitten_h_INCLUDED

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct kitten kitten;

kitten *kitten_init (void);
void kitten_clear (kitten *);
void kitten_release (kitten *);

void kitten_track_antecedents (kitten *);

void kitten_shuffle_clauses (kitten *);
void kitten_flip_phases (kitten *);
void kitten_randomize_phases (kitten *);

void kitten_assume (kitten *, unsigned lit);

void kitten_clause (kitten *, size_t size, unsigned *);
void kitten_unit (kitten *, unsigned);
void kitten_binary (kitten *, unsigned, unsigned);

void kitten_clause_with_id_and_exception (kitten *, unsigned id,
                                          size_t size, const unsigned *,
                                          unsigned except);

void kitten_no_ticks_limit (kitten *);
void kitten_set_ticks_limit (kitten *, uint64_t);
void kitten_no_terminator (kitten *);
void kitten_set_terminator (kitten *, void *, int (*)(void *));

int kitten_solve (kitten *);
int kitten_status (kitten *);

signed char kitten_value (kitten *, unsigned);
bool kitten_failed (kitten *, unsigned);
bool kitten_flip_literal (kitten *, unsigned);

unsigned kitten_compute_clausal_core (kitten *, uint64_t *learned);
void kitten_shrink_to_clausal_core (kitten *);

void kitten_traverse_core_ids (kitten *, void *state,
                               void (*traverse) (void *state, unsigned id));

void kitten_traverse_core_clauses (kitten *, void *state,
                                   void (*traverse) (void *state,
                                                     bool learned, size_t,
                                                     const unsigned *));
void kitten_trace_core (kitten *, void *state,
                        void (*trace) (void *, unsigned, unsigned, bool,
                                       size_t, const unsigned *,
                                       size_t, const unsigned *));

struct kissat;
kitten *kitten_embedded (struct kissat *);

#ifdef IPASIR_KITTEN
const char *ipasir_signature (void);
void *ipasir_init (void);
void ipasir_release (void *);
void ipasir_add (void *, int);
void ipasir_assume (void *, int);
int ipasir_solve (void *);
int ipasir_val (void *, int);
int ipasir_failed (void *, int);
void ipasir_set_terminate (void *, void *, int (*)(void *));
void ipasir_trace_proof (void *, FILE *);
#endif

#ifdef __cplusplus
}
#endif

#endif
