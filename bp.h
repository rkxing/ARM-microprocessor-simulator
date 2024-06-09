#ifndef _BP_H_
#define _BP_H_

#include <stdint.h>
#include <stdbool.h>

#define PHTSIZE 256
#define BTBSIZE 1024

typedef struct {
    uint64_t tag;
    bool valid;
    bool is_conditional;
    uint64_t target;
} BTB_entry_t;

typedef uint8_t uint2_t; // Would have liked to use uint2_t if C had it.

typedef struct
{
    /* gshare */
    uint8_t GHR;
    uint2_t PHT[PHTSIZE];
    /* BTB */
    BTB_entry_t BTB[BTBSIZE];
} bp_t;

extern bp_t BP_data; // Holds the full state needed for branch prediction

void _2_bit_incr(uint2_t *data);
void _2_bit_decr(uint2_t *data);

void bp_init();
void bp_predict(uint64_t PC, uint64_t* predicted_pc, bool* predicted_taken);
void bp_update(bool is_conditional, bool taken, uint64_t PC, uint64_t target);

void flush_pipeline();

#endif