#include "bp.h"
#include "pipe.h"
#include "utils.h"
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

// must define here because of extern declaration in pipe.h
pipe_reg_DE_EX_t pipe_reg_DE_EX;
pipe_reg_IF_DE_t pipe_reg_IF_DE;

bp_t BP_data;


void _2_bit_incr(uint2_t *data) {
    if(*data < 3)
        (*data)++;
}

void _2_bit_decr(uint2_t *data) {
    if(*data > 0)
        (*data)--;
}

void bp_init() {
    BP_data.GHR = 0;
    for(size_t i=0; i<PHTSIZE; i++)
        BP_data.PHT[i] = 0;
    for(size_t i=0; i<BTBSIZE; i++)
        BP_data.BTB[i] = (BTB_entry_t) { // This is not actually necessary
            .tag=0,                      // since C default initializes static
            .valid=0,                    // vars to 0. This is here just for explicitness.
            .is_conditional=0,
            .target=0
        };
}

void bp_predict(uint64_t PC, uint64_t* predicted_pc, bool* predicted_taken)
{
    BTB_entry_t e = BP_data.BTB[truncator64(PC, 2, 12)];
    uint8_t pht_idx = BP_data.GHR ^ (uint8_t)truncator64(PC, 2, 10);

    *predicted_taken = false;
    if(e.tag != PC || !e.valid) { // BTB miss
        *predicted_pc = PC + 4;
        return;
    }

    if(e.is_conditional == false || BP_data.PHT[pht_idx] > 1) {
        *predicted_pc = e.target;
        *predicted_taken = true;
        return;
    }
    *predicted_pc = PC + 4;
    return;
}

void bp_update(bool is_conditional, bool taken, uint64_t PC, uint64_t target)
{
    /* Update BTB */
    BTB_entry_t* e = &BP_data.BTB[truncator64(PC, 2, 12)];
    uint8_t pht_idx = BP_data.GHR ^ truncator64(PC, 2, 10);
    e->tag = PC;
    e->valid = true;
    e->is_conditional = is_conditional;
    e->target = target;

    if(is_conditional) {
        /* Update gshare directional predictor */
        if(taken)
            _2_bit_incr(&(BP_data.PHT[pht_idx]));
        else
            _2_bit_decr(&(BP_data.PHT[pht_idx]));

        /* Update global history register */
        BP_data.GHR <<= 1;
        BP_data.GHR |= (uint8_t)taken;
    }
}

/*
This should flush only the IF_DE and DE_EX regs
*/
void flush_pipeline() {
    pipe_reg_IF_DE.to_flush = true;
}
