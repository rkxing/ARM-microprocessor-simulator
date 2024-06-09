#include "pipe.h"
#include "shell.h"
#include "cache.h"
#include "bp.h"
#include "utils.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

#define DEBUGGING_LOG "debug_branching_pipe_regs.txt"
#define DEBUGGING_LOG2 "debug_branching_btb.txt"
// Change to /dev/null to suppress logs

// must define here because of extern declaration in pipe.h
int RUN_BIT;
pipe_reg_IF_DE_t pipe_reg_IF_DE;
pipe_reg_DE_EX_t pipe_reg_DE_EX;
pipe_reg_EX_MEM_t pipe_reg_EX_MEM;
pipe_reg_MEM_WB_t pipe_reg_MEM_WB;

/* global pipeline state */
CPU_State CURRENT_STATE, NEXT_STATE;

// global pipeline register values to indicate if they have been used before
bool init_IF_DE = false;
bool init_DE_EX = false;
bool init_EX_MEM = false;
bool init_MEM_WB = false;

// globals to indicate whether a halt is in place and what stages need to be
// completed before halting the simulation
bool FE_halted = false;
bool DE_halted = false;
bool EX_halted = false;
bool MEM_halted = false;

// bool branch_unresolved = false;
bool init_control_stall = false;
bool data_stalled = false; // Concerns IF, DE and EX, but NOT MEM or WB
bool control_stalled = false; // Concerns IFonly
bool wait_i_cache = false;
bool wait_d_cache = false;

char* to_bin_str_32(uint32_t num) {
    static char binaryStr[65]; // 64 bits + 1 for null terminator
    int i;
    for (i = 31; i >= 0; i--) {
        binaryStr[i] = (num & 1) + '0';
        num >>= 1;
    }
    binaryStr[32] = '\0'; // Null-terminate the string
    return binaryStr;
}

void print_cache_contents(cache_t* c) {
    cache_line_t curr;
    printf("-----START PRINT CACHE-----\n");
    for (int i = 0; i < c->num_sets; i++) {
        for (int j = 0; j < c->num_lines; j++) {
            curr = c->lines[i][j];
            if (!curr.valid_bit)
                continue;
            printf("set: %d | line: %d | tag: %ld | timestamp: %ld\n", i, j, curr.tag, curr.used_timestamp);
            printf("data: ");
            for (int k = 0; k < BLOCK_SIZE; k++) {
                printf("%d ", curr.data[k]);
            }
            printf("\n");
        }
    }
    printf("-----END PRINT CACHE-----\n");
}

void print_pipe_reg_IF_DE() {
    FILE *fp = fopen(DEBUGGING_LOG, "a");
    fprintf(fp, "----- Printing out the print_pipe_reg_IF_DE -----\n");
    fprintf(fp, "instruction_full: %u=0b%s\n", pipe_reg_IF_DE.Instruction_full, to_bin_str_32(pipe_reg_IF_DE.Instruction_full));
    fprintf(fp, "to_squash: %d\n", pipe_reg_IF_DE.to_squash);
    fprintf(fp, "predicted_taken: %d\n", pipe_reg_IF_DE.predicted_taken);
    fprintf(fp, "global.init_control_stall: %d\n", init_control_stall);
    fprintf(fp, "global.control_stalled: %d\n", control_stalled);
    fprintf(fp, "global.data_stalled: %d\n", data_stalled);
    fprintf(fp, "global.CURRENT_STATE.PC: 0x%lx\n", CURRENT_STATE.PC);
    fprintf(fp, "global.FE_halted: ");
    fprintf(fp, FE_halted?"true\n":"false\n");
    fprintf(fp, "global.DE_halted: ");
    fprintf(fp, DE_halted?"true\n":"false\n");
    fprintf(fp, "global.EX_halted: ");
    fprintf(fp, EX_halted?"true\n":"false\n");
    fprintf(fp, "global.MEM_halted: ");
    fprintf(fp, MEM_halted?"true\n":"false\n");
    fclose(fp);
}

void print_pipe_reg_DE_EX() {
    FILE *fp = fopen(DEBUGGING_LOG, "a");
    fprintf(fp, "----- Printing out the print_pipe_reg_DE_EX -----\n");
    fprintf(fp, "EX.ALUSrc: %d\n", pipe_reg_DE_EX.EX.ALUSrc);
    fprintf(fp, "EX.ALUOp: %d\n", pipe_reg_DE_EX.EX.ALUOp);
    fprintf(fp, "M.ConfirmedBranch: %d\n", pipe_reg_DE_EX.M.ConfirmedBranch);
    fprintf(fp, "M.BranchIfZero: %d\n", pipe_reg_DE_EX.M.BranchIfZero);
    fprintf(fp, "M.MemRead: %d\n", pipe_reg_DE_EX.M.MemRead);
    fprintf(fp, "M.MemWrite: %d\n", pipe_reg_DE_EX.M.MemWrite);
    fprintf(fp, "M.DataSize: %zu\n", pipe_reg_DE_EX.M.DataSize);
    fprintf(fp, "WB.RegWrite: %d\n", pipe_reg_DE_EX.WB.RegWrite);
    fprintf(fp, "WB.MemtoReg: %d\n", pipe_reg_DE_EX.WB.MemtoReg);
    fprintf(fp, "WB.SetFlags: %d\n", pipe_reg_DE_EX.WB.SetFlags);
    fprintf(fp, "predicted_taken: %d\n", pipe_reg_DE_EX.predicted_taken);
    fprintf(fp, "predicted_pc: %lu=0b%s\n", pipe_reg_DE_EX.predicted_pc, to_bin_str_32(pipe_reg_DE_EX.predicted_pc));
    //printf("State: %d\n", pipe_reg_DE_EX.State);
    fprintf(fp, "inst_type: ");
    switch (pipe_reg_DE_EX.inst_type) {
        case INST_OPERATE:
            fprintf(fp, "INST_OPERATE\n");
            break;
        case INST_DATAMOV:
            fprintf(fp, "INST_DATAMOV\n");
            break;
        case INST_CONTROL:
            fprintf(fp, "INST_CONTROL\n");
            break;
        case INST_OTHEROP:
            fprintf(fp, "INST_OTHEROP\n");
            break;
        case INST_INVALID:
            fprintf(fp, "INST_INVALID\n");
            break;
        case INST_DBUBBLE:
            fprintf(fp, "INST_DBUBBLE\n");
            break;
        case INST_CBUBBLE:
            fprintf(fp, "INST_CBUBBLE\n");
            break;
        case INST_MEMBUBBLE:
            fprintf(fp, "INST_MEMBUBBLE\n");
            break;
        default:
            fprintf(fp, "Unknown\n");
    }
    fprintf(fp, "Read_data_1: %lu\n", pipe_reg_DE_EX.Read_data_1);
    fprintf(fp, "Read_data_2: %lu\n", pipe_reg_DE_EX.Read_data_2);
    fprintf(fp, "Sign_extended_frag: %ld\n", pipe_reg_DE_EX.Sign_extended_frag);
    fprintf(fp, "Instruction_31_21: %u\n", pipe_reg_DE_EX.Instruction_31_21);
    fprintf(fp, "Instruction_4_0: %u\n", pipe_reg_DE_EX.Instruction_4_0);
    fprintf(fp, "global.init_control_stall: %d\n", init_control_stall);
    fprintf(fp, "global.control_stalled: %d\n", control_stalled);
    fprintf(fp, "global.data_stalled: %d\n", data_stalled);
    fprintf(fp, "global.CURRENT_STATE.PC: 0x%lx\n", CURRENT_STATE.PC);
    fprintf(fp, "global.FE_halted: ");
    fprintf(fp, FE_halted?"true\n":"false\n");
    fprintf(fp, "global.DE_halted: ");
    fprintf(fp, DE_halted?"true\n":"false\n");
    fprintf(fp, "global.EX_halted: ");
    fprintf(fp, EX_halted?"true\n":"false\n");
    fprintf(fp, "global.MEM_halted: ");
    fprintf(fp, MEM_halted?"true\n":"false\n");
    fclose(fp);
}

void print_pipe_reg_EX_MEM() {
    FILE *fp = fopen(DEBUGGING_LOG, "a");
    fprintf(fp, "----- Printing out the print_pipe_reg_EX_MEM -----\n");
    fprintf(fp, "inst_type: ");
    switch (pipe_reg_EX_MEM.inst_type) {
        case INST_OPERATE:
            fprintf(fp, "INST_OPERATE\n");
            break;
        case INST_DATAMOV:
            fprintf(fp, "INST_DATAMOV\n");
            break;
        case INST_CONTROL:
            fprintf(fp, "INST_CONTROL\n");
            break;
        case INST_OTHEROP:
            fprintf(fp, "INST_OTHEROP\n");
            break;
        case INST_INVALID:
            fprintf(fp, "INST_INVALID\n");
            break;
        case INST_DBUBBLE:
            fprintf(fp, "INST_DBUBBLE\n");
            break;
        case INST_CBUBBLE:
            fprintf(fp, "INST_CBUBBLE\n");
            break;
        case INST_MEMBUBBLE:
            fprintf(fp, "INST_MEMBUBBLE\n");
            break;
        default:
            fprintf(fp, "Unknown\n");
    }
    fprintf(fp, "M.ConfirmedBranch: %d\n", pipe_reg_EX_MEM.M.ConfirmedBranch);
    fprintf(fp, "M.BranchIfZero: %d\n", pipe_reg_EX_MEM.M.BranchIfZero);
    fprintf(fp, "M.MemRead: %d\n", pipe_reg_EX_MEM.M.MemRead);
    fprintf(fp, "M.MemWrite: %d\n", pipe_reg_EX_MEM.M.MemWrite);
    fprintf(fp, "M.DataSize: %zu\n", pipe_reg_EX_MEM.M.DataSize);
    fprintf(fp, "WB.RegWrite: %d\n", pipe_reg_EX_MEM.WB.RegWrite);
    fprintf(fp, "WB.MemtoReg: %d\n", pipe_reg_EX_MEM.WB.MemtoReg);
    fprintf(fp, "WB.SetFlags: %d\n", pipe_reg_EX_MEM.WB.SetFlags);
    //fprintf(fp, "State: %d\n", pipe_reg_EX_MEM.State);
    fprintf(fp, "calculated_PC: %lu\n", pipe_reg_EX_MEM.calculated_PC);
    fprintf(fp, "Read_data_2: %lu\n", pipe_reg_EX_MEM.Read_data_2);
    fprintf(fp, "ALUZero: %d\n", pipe_reg_EX_MEM.ALUZero);
    fprintf(fp, "ALUresult: %lu\n", pipe_reg_EX_MEM.ALUresult);
    fprintf(fp, "Instruction_4_0: %u\n", pipe_reg_EX_MEM.Instruction_4_0);
    fprintf(fp, "global.init_control_stall: %d\n", init_control_stall);
    fprintf(fp, "global.control_stalled: %d\n", control_stalled);
    fprintf(fp, "global.data_stalled: %d\n", data_stalled);
    fprintf(fp, "global.CURRENT_STATE.PC: 0x%lx\n", CURRENT_STATE.PC);
    fprintf(fp, "global.FE_halted: ");
    fprintf(fp, FE_halted?"true\n":"false\n");
    fprintf(fp, "global.DE_halted: ");
    fprintf(fp, DE_halted?"true\n":"false\n");
    fprintf(fp, "global.EX_halted: ");
    fprintf(fp, EX_halted?"true\n":"false\n");
    fprintf(fp, "global.MEM_halted: ");
    fprintf(fp, MEM_halted?"true\n":"false\n");
    fclose(fp);
}

void print_pipe_reg_MEM_WB() {
    FILE *fp = fopen(DEBUGGING_LOG, "a");
    fprintf(fp, "----- Printing out the print_pipe_reg_MEM_WB -----\n");
    fprintf(fp, "inst_type: ");
    switch (pipe_reg_MEM_WB.inst_type) {
        case INST_OPERATE:
            fprintf(fp, "INST_OPERATE\n");
            break;
        case INST_DATAMOV:
            fprintf(fp, "INST_DATAMOV\n");
            break;
        case INST_CONTROL:
            fprintf(fp, "INST_CONTROL\n");
            break;
        case INST_OTHEROP:
            fprintf(fp, "INST_OTHEROP\n");
            break;
        case INST_DBUBBLE:
            fprintf(fp, "INST_DBUBBLE\n");
            break;
        case INST_CBUBBLE:
            fprintf(fp, "INST_CBUBBLE\n");
            break;
        case INST_MEMBUBBLE:
            fprintf(fp, "INST_MEMBUBBLE\n");
            break;
        case INST_INVALID:
            fprintf(fp, "INST_INVALID\n");
            break;
        default:
            fprintf(fp, "Unknown\n");
    }
    fprintf(fp, "M.ConfirmedBranch: %d\n", pipe_reg_MEM_WB.M.ConfirmedBranch);
    fprintf(fp, "M.BranchIfZero: %d\n", pipe_reg_MEM_WB.M.BranchIfZero);
    fprintf(fp, "M.MemRead: %d\n", pipe_reg_MEM_WB.M.MemRead);
    fprintf(fp, "M.MemWrite: %d\n", pipe_reg_MEM_WB.M.MemWrite);
    fprintf(fp, "M.DataSize: %zu\n", pipe_reg_MEM_WB.M.DataSize);
    fprintf(fp, "WB.RegWrite: %d\n", pipe_reg_MEM_WB.WB.RegWrite);
    fprintf(fp, "WB.MemtoReg: %d\n", pipe_reg_MEM_WB.WB.MemtoReg);
    fprintf(fp, "WB.SetFlags: %d\n", pipe_reg_MEM_WB.WB.SetFlags);
    fprintf(fp, "ALUresult: %lu\n", pipe_reg_MEM_WB.ALUresult);
    fprintf(fp, "Read_data: %lu\n", pipe_reg_MEM_WB.Read_data);
    fprintf(fp, "Instruction_4_0: %u\n", pipe_reg_MEM_WB.Instruction_4_0);
    fprintf(fp, "global.init_control_stall: %d\n", init_control_stall);
    fprintf(fp, "global.control_stalled: %d\n", control_stalled);
    fprintf(fp, "global.data_stalled: %d\n", data_stalled);
    fprintf(fp, "global.CURRENT_STATE.PC: 0x%lx\n", CURRENT_STATE.PC);
    fprintf(fp, "global.FE_halted: ");
    fprintf(fp, FE_halted?"true\n":"false\n");
    fprintf(fp, "global.DE_halted: ");
    fprintf(fp, DE_halted?"true\n":"false\n");
    fprintf(fp, "global.EX_halted: ");
    fprintf(fp, EX_halted?"true\n":"false\n");
    fprintf(fp, "global.MEM_halted: ");
    fprintf(fp, MEM_halted?"true\n":"false\n");
    fclose(fp);
}

void print_bp_data() {
    FILE *fp = fopen(DEBUGGING_LOG2, "a");
    fprintf(fp, "\n----- At the beginning of cycle %d -----\n\n", stat_cycles+1);

    if (fp == NULL) {
        perror("Error opening debug file");
        return;
    }

    fprintf(fp, "GHR: %d\n", BP_data.GHR);

    fprintf(fp, "PHT:\n");
    for (int i = 0; i < PHTSIZE; ++i) {
        fprintf(fp, "%d ", BP_data.PHT[i]);
        if ((i + 1) % 16 == 0) fprintf(fp, "\n");
    }

    fprintf(fp, "BTB:\n");
    for (int i = 0; i < BTBSIZE; ++i) {
        fprintf(fp, "Entry %d:\n", i);
        fprintf(fp, "  Tag: 0x%lx\n", BP_data.BTB[i].tag);
        fprintf(fp, "  Valid: %s\n", BP_data.BTB[i].valid ? "true" : "false");
        fprintf(fp, "  Conditional: %s\n", BP_data.BTB[i].is_conditional ? "true" : "false");
        fprintf(fp, "  Target: 0x%lx\n", BP_data.BTB[i].target);
    }

    fprintf(fp, "\n----- Finished printing BTB at the beginning of cycle %d -----\n\n", stat_cycles+1);

    fclose(fp);
}

void unit_control_r_format(
    interface_WB *WB,
    interface_M *M,
    interface_EX *EX,
    instruction_layout_t *layout
) {
    WB->RegWrite = true;
    WB->MemtoReg = false;
    M->ConfirmedBranch = false;
    M->BranchIfZero = false;
    M->MemRead = false;
    M->MemWrite = false;
    // M->DataSize is irrelevant
    // EX->ALUOp shall be assigned in the calling subroutine
    EX->ALUSrc = false; // Second operand is not immediate (but register)
    // *frag is irrelevant
    *layout = INST_R;
}

void unit_control_i_format(
    instruction_t inst,
    interface_WB *WB,
    interface_M *M,
    interface_EX *EX,
    int64_t *frag,
    instruction_layout_t *layout
) {
    WB->RegWrite = true;
    WB->MemtoReg = false;
    M->ConfirmedBranch = false;
    M->BranchIfZero = false;
    M->MemRead = false;
    M->MemWrite = false;
    //M->DataSize is irrelevant
    // EX->ALUOp shall be assigned in the calling subroutine
    EX->ALUSrc = true; // Second operand is immediate
    *frag = sign_extend_64(inst, 10, 22);
    *layout = INST_I;
}

void unit_control_d_format(
    instruction_t inst,
    interface_WB *WB,
    interface_M *M,
    interface_EX *EX,
    int64_t *frag,
    instruction_layout_t *layout
) {
    // WB->RegWrite and
    // WB->MemtoReg shall be assigned in the calling subroutine
    WB->SetFlags = false;
    M->ConfirmedBranch = false;
    M->BranchIfZero = false;
    // M->MemRead,
    // M->MemWrite and
    // M->DataSize shall be assigned in the calling subroutine
    EX->ALUOp = OP_ADD;
    EX->ALUSrc = true; // Second operand is immediate
    *frag = sign_extend_64(inst, 12, 21);
    *layout = INST_D;
}

void create_d_bubble() {
    pipe_reg_DE_EX.WB.RegWrite = false;
    // WB.toReg is irrelevant
    pipe_reg_DE_EX.WB.SetFlags = false;
    pipe_reg_DE_EX.M.ConfirmedBranch = false;
    pipe_reg_DE_EX.M.BranchIfZero = false;
    pipe_reg_DE_EX.M.MemRead = false;
    pipe_reg_DE_EX.M.MemWrite = false;
    // M.DataSize,
    // EX.ALUSrc, and
    // EX.ALUOp are irrelevant
    pipe_reg_DE_EX.inst_type = INST_DBUBBLE;
    pipe_reg_DE_EX.inst_layout = INST_NOP;
}

void create_c_bubble() {
    pipe_reg_DE_EX.WB.RegWrite = false;
    // WB.toReg is irrelevant
    pipe_reg_DE_EX.WB.SetFlags = false;
    pipe_reg_DE_EX.M.ConfirmedBranch = false;
    pipe_reg_DE_EX.M.BranchIfZero = false;
    pipe_reg_DE_EX.M.MemRead = false;
    pipe_reg_DE_EX.M.MemWrite = false;
    // M.DataSize,
    // EX.ALUSrc, and
    // EX.ALUOp are irrelevant
    pipe_reg_DE_EX.inst_type = INST_CBUBBLE;
    pipe_reg_DE_EX.inst_layout = INST_NOP;
}

void create_mem_bubble() {
    pipe_reg_DE_EX.WB.RegWrite = false;
    // WB.toReg is irrelevant
    pipe_reg_DE_EX.WB.SetFlags = false;
    pipe_reg_DE_EX.M.ConfirmedBranch = false;
    pipe_reg_DE_EX.M.BranchIfZero = false;
    pipe_reg_DE_EX.M.MemRead = false;
    pipe_reg_DE_EX.M.MemWrite = false;
    // M.DataSize,
    // EX.ALUSrc, and
    // EX.ALUOp are irrelevant
    pipe_reg_DE_EX.inst_type = INST_MEMBUBBLE;
    pipe_reg_DE_EX.inst_layout = INST_NOP;
}

int64_t sign_extend_64(uint32_t data, size_t begin, size_t end) {
    uint64_t res = (uint64_t) ((data & (1 << end)-1) >> begin);
    // if signed
    if (res & ((uint64_t)1 << (end - begin - 1))) {
        res = res | (0xffffffffffffffff << (end-begin));
    }
    return (int64_t)res;
}

instruction_t fetch(uint64_t addr)
{
    // on a hit, inst can be returned immediately (remaining_cycles == 0)
    // on a miss, start a 10 cycle stall, and the inst should be returned on the 11th cycle
    query_state_t inst_query = cache_read_handler(i_cache, addr, 4);
    wait_i_cache = inst_query.remaining_cycles > 0;
    return inst_query.data; // Could be garbage if wait_i_cache==true,
                            // but nothing else we can do.
}

void set_flags(uint64_t result, CPU_State *state) {
    state->FLAG_N = ((int64_t)result < 0);
    state->FLAG_Z = (result == 0);
}

// ALU
void unit_ALU(mop_operate_type_t op_type, uint64_t operand1, uint64_t operand2, uint64_t* ALU_result, bool* is_zero)
{
    uint64_t result;
    bool incr_pc = true;
    switch(op_type) {

    case OP_ADD:
        *ALU_result = operand1 + operand2;
        break;

    case OP_ADDS:
        *ALU_result = operand1 + operand2;
        pipe_reg_EX_MEM.WB.SetFlags = true;
        break;

    case OP_AND:
        *ALU_result = operand1 & operand2;
        break;

    case OP_ANDS:
        *ALU_result = operand1 & operand2;
        pipe_reg_EX_MEM.WB.SetFlags = true;
        // previously, this case computed result and stored it in the dest:
        // result =   unified_load_64(m.inst.operate.src1)
        //          & unified_load_64(m.inst.operate.src2);
        // unified_store_64(m.inst.operate.dest, result);
        break;

    case OP_EOR:
        *ALU_result = operand1 ^ operand2;
        break;

    case OP_ORR:
        *ALU_result = operand1 | operand2;
        break;

    case OP_LSL:
        *ALU_result = operand1 << (-operand2 % 64 - 1);
        break;

    case OP_LSR:
        *ALU_result = operand1 >> operand2;
        break;

    case OP_SUB:
        *ALU_result = operand1 - operand2;
        break;

    case OP_SUBS:
    //case OP_CMP: // Note: OP_SUBS encompasses OP_CMP
        *ALU_result = operand1 - operand2;
        pipe_reg_EX_MEM.WB.SetFlags = true;
        break;

    case OP_MUL:
        *ALU_result = operand1 * operand2;
        break;

    case OP_PASSTHRU_2_S:
        *ALU_result = operand2;
        break;

    default: // A corrupted entry
        assert(0);
    }

    *is_zero = *ALU_result == 0;
}


void unit_control(
    uint32_t inst,
    CPU_State state,
    interface_WB *WB,
    interface_M *M,
    interface_EX *EX,
    int64_t *frag,
    instruction_layout_t *layout,
    instruction_type_t *type
) {
    uint32_t opcode = truncator32(inst, 21, 32);
    uint32_t shamt;
    shamt = truncator32(inst, 10, 16); // assertions maybe not necessary
    uint32_t test = 0xffffffff;

    bool is_ADDS_extended_register = false;
    bool is_ADDS_immediate = false;
    bool is_ANDS_shifted = false;
    bool is_SUBS_extended_register = false;
    bool is_SUBS_immediate = false;
    bool is_load = false;
    bool is_lsr = false;

    uint32_t cond = truncator32(inst, 0, 4);

    // Keeps the highest 11 bits.
    // Suffices for R- and D-format instructions.
    // Overshoots for I-format instructions; will just use more cases.
    switch(opcode) {
    // ---------- INST_OPERATE ----------

    // Note: the asm2hex script defaults to shifted register encodings of
    // instructions when the extended register versions are used, so
    // these opcodes must be encompassed as well.

    // ADDS (extended register)
    case 0b10101011000:
    case 0b10101011001:
        is_ADDS_extended_register = true;
        // [[fallthrough]]
    // ADD (extended register)
    case 0b10001011000:
    case 0b10001011001:
        *type = INST_OPERATE;
        EX->ALUOp = (is_ADDS_extended_register) ? OP_ADDS : OP_ADD;
        WB->SetFlags = is_ADDS_extended_register;
        unit_control_r_format(WB, M, EX, layout);
        break;

    // ADDS (immediate)
    case 0b10110001000:
    case 0b10110001001:
    case 0b10110001010:
    case 0b10110001011:
    case 0b10110001100:
    case 0b10110001101:
    case 0b10110001110:
    case 0b10110001111:
        is_ADDS_immediate = true;
        // [[fallthrough]]
    // ADD (immediate)
    case 0b10010001000:
    case 0b10010001001:
    case 0b10010001010:
    case 0b10010001011:
    case 0b10010001100:
    case 0b10010001101:
    case 0b10010001110:
    case 0b10010001111:
        *type = INST_OPERATE;
        EX->ALUOp = (is_ADDS_immediate) ? OP_ADDS : OP_ADD;
        WB->SetFlags = is_ADDS_immediate;
        unit_control_i_format(inst, WB, M, EX, frag, layout);
        break;

    // ANDS (shifted register)
    case 0b11101010000:
        is_ANDS_shifted = true;
        // [[fallthrough]]
    // AND (shifted register)
    case 0b10001010000:
	*type = INST_OPERATE;
        EX->ALUOp = (is_ANDS_shifted) ? OP_ANDS : OP_AND;
        WB->SetFlags = is_ANDS_shifted;
        unit_control_r_format(WB, M, EX, layout);
        break;

    // EOR (shifted register)
    case 0b11001010000:
        assert(shamt == 0);
        *type = INST_OPERATE;
        EX->ALUOp = OP_EOR;
        WB->SetFlags = false;
        unit_control_r_format(WB, M, EX, layout);
        break;

    // ORR (shifted register)
    case 0b10101010000:
        assert(shamt == 0);
        *type = INST_OPERATE;
        EX->ALUOp = OP_ORR;
        WB->SetFlags = false;
        unit_control_r_format(WB, M, EX, layout);
        break;

    // LSL & LSR (immediate)
    // These instructions share the same opcode(s),
    // but LSR occurs when shamt = 0b111111
    case 0b11010011010:
    case 0b11010011011:
        is_lsr = (shamt == 0b111111);
        *type = INST_OPERATE;
        EX->ALUOp = is_lsr ? OP_LSR : OP_LSL;
        unit_control_i_format(inst, WB, M, EX, frag, layout);
        break;

    // SUBS (extended register) / CMP (extended register)
    case 0b11101011000: // shifted register
    case 0b11101011001:
        is_SUBS_extended_register = true;
    // SUB (extended register)
    case 0b11001011000: // shifted register
    case 0b11001011001:
        *type = INST_OPERATE;
        EX->ALUOp = (is_SUBS_extended_register) ? OP_SUBS : OP_SUB;
        WB->SetFlags = is_SUBS_extended_register;
        unit_control_r_format(WB, M, EX, layout);
        break;

    // SUBS (immediate) / CMP (immediate)
    // # Note: shift bits always 00, so only these cases needed
    case 0b11110001000:
    case 0b11110001001:
        is_SUBS_immediate = true;
    // SUB (immediate)
    case 0b11010001000:
    case 0b11010001001:
        *type = INST_OPERATE;
        EX->ALUOp = (is_SUBS_immediate) ? OP_SUBS : OP_SUB;
        WB->SetFlags = is_SUBS_immediate;
        unit_control_i_format(inst, WB, M, EX, frag, layout);
        break;

    // MUL
    case 0b10011011000:
        *type = INST_OPERATE;
        EX->ALUOp = OP_MUL;
        WB->SetFlags = false;
        unit_control_i_format(inst, WB, M, EX, frag, layout);
        break;


    // ---------- INST_DATAMOV ----------

    // LDUR (64-bit)
    case 0b11111000010:
    // LDUR (32-bit)
    case 0b10111000010:
    // LDURH (16-bit)
    case 0b01111000010:
    // LDURB (8-bit)
    case 0b00111000010:
        is_load = true;
    // STUR (64-bit)
    case 0b11111000000:
    // STUR (32-bit)
    case 0b10111000000:
    // STURH (16-bit)
    case 0b01111000000:
    // STURB (8-bit)
    case 0b00111000000:
        assert(truncator32(inst, 5, 10) != 31); // SP
        *type = INST_DATAMOV;
        unit_control_d_format(inst, WB, M, EX, frag, layout);

        M->DataSize = 8 << truncator32(inst, 30, 32);
        // These four instructions only differ in the first two bits:
        // 00 -> 8-bits, 01 -> 16-bits, 10 -> 32-bits, 11 -> 64-bits,
        // allowing for such unified treatment.
        if (is_load) { // LDUR
            WB->RegWrite = true;
            WB->MemtoReg = true;
            M->MemRead = true;
            M->MemWrite = false;
        }
        else { // STUR
            WB->RegWrite = false;
            WB->MemtoReg = false; // Doesn't really matter
            M->MemRead = false;
            M->MemWrite = true;
        }
        break;

    // MOVZ
    case 0b11010010100:
    case 0b11010010101:
    case 0b11010010110:
    case 0b11010010111:
        assert(truncator32(inst, 21, 23) == 0);
        *type = INST_DATAMOV;
        // MOVZ operates diff than LDUR and STUR so we cannot use this function
        // unit_control_d_format(inst, WB, M, EX, frag);
        EX->ALUOp = OP_PASSTHRU_2_S;
        EX->ALUSrc = true;
        *frag = sign_extend_64(inst, 5, 21);
        WB->RegWrite = true;
        WB->MemtoReg = false;
        M->DataSize = 16;
        M->MemRead = false;
        M->MemWrite = false;
        M->ConfirmedBranch = false;
        M->BranchIfZero = false;
        *layout = INST_IM;
        break;


    // ---------- INST_CONTROL ----------

    // BR
    case 0b11010110000:
        *type = INST_CONTROL;
        WB->RegWrite = false;
        // WB->MemtoReg is irrelevant
        WB->SetFlags = false;
        M->ConfirmedBranch = true;
        M->BranchIfZero = false;
        M->MemRead = false;
        M->MemWrite = false;
        // M->DataSize is irrelevant
        EX->ALUSrc = true;
        EX->ALUOp = OP_PASSTHRU_2_S;
        EX->b_type = BR;
        init_control_stall = true;
        *frag = CURRENT_STATE.REGS[truncator32(inst, 5, 10)] - state.PC;
        *layout = INST_BR;
        break;

    // B
    case 0b00010100000:
    case 0b00010100001:
    case 0b00010100010:
    case 0b00010100011:
    case 0b00010100100:
    case 0b00010100101:
    case 0b00010100110:
    case 0b00010100111:
    case 0b00010101000:
    case 0b00010101001:
    case 0b00010101010:
    case 0b00010101011:
    case 0b00010101100:
    case 0b00010101101:
    case 0b00010101110:
    case 0b00010101111:
    case 0b00010110000:
    case 0b00010110001:
    case 0b00010110010:
    case 0b00010110011:
    case 0b00010110100:
    case 0b00010110101:
    case 0b00010110110:
    case 0b00010110111:
    case 0b00010111000:
    case 0b00010111001:
    case 0b00010111010:
    case 0b00010111011:
    case 0b00010111100:
    case 0b00010111101:
    case 0b00010111110:
    case 0b00010111111:
        *type = INST_CONTROL;
        WB->RegWrite = false;
        // WB->MemtoReg is irrelevant
        WB->SetFlags = false;
        M->ConfirmedBranch = true;
        M->BranchIfZero = false;
        M->MemRead = false;
        M->MemWrite = false;
        // M->DataSize,
        // EX->ALUSrc and
        // EX->ALUOp are irrelevant
        EX->b_type = B;
        init_control_stall = true;
        *frag = sign_extend_64(inst, 0, 26);
        *layout = INST_B;
        break;

    // CBNZ
    case 0b10110101000:
    case 0b10110101001:
    case 0b10110101010:
    case 0b10110101100:
    case 0b10110101011:
    case 0b10110101110:
    case 0b10110101101:
    case 0b10110101111:
        *type = INST_CONTROL;
        WB->RegWrite = false;
        // WB->MemtoReg is irrelevant
        WB->SetFlags = false;
        M->ConfirmedBranch = false;
        M->BranchIfZero = false;
        M->MemRead = false;
        M->MemWrite = false;
        // M->DataSize is irrelevant
        EX->ALUSrc = 0;
        EX->ALUOp = OP_NOT_2_S;
        EX->b_type = CBNZ;
        init_control_stall = true;
        *frag = sign_extend_64(inst, 5, 24);
        *layout = INST_CB;
        break;

    // CBZ
    case 0b10110100000:
    case 0b10110100001:
    case 0b10110100010:
    case 0b10110100100:
    case 0b10110100011:
    case 0b10110100110:
    case 0b10110100101:
    case 0b10110100111:
        *type = INST_CONTROL;
        WB->RegWrite = false;
        // WB->MemtoReg is irrelevant
        WB->SetFlags = false;
        M->ConfirmedBranch = false;
        M->BranchIfZero = false;
        M->MemRead = false;
        M->MemWrite = false;
        // M->DataSize is irrelevant
        EX->ALUSrc = 0;
        EX->ALUOp = OP_PASSTHRU_2_S;
        EX->b_type = CBZ;
        init_control_stall = true;
        *frag = sign_extend_64(inst, 5, 24);
        *layout = INST_CB;
        break;

    // B.cond all have same opcodes, but with diff cond values in last 4 bits
    case 0b01010100000:
    case 0b01010100001:
    case 0b01010100010:
    case 0b01010100100:
    case 0b01010100011:
    case 0b01010100101:
    case 0b01010100110:
    case 0b01010100111:
        *type = INST_CONTROL;
        switch (cond) { // C and V flags assumed to be 0
            // BEQ
            case 0b0000:
                EX->b_type = BEQ;
                break;
            // BNE
            case 0b0001:
                EX->b_type = BNE;
                break;
            // BGT
            case 0b1100:
                EX->b_type = BGT;
                break;
            // BLT
            case 0b1011:
                EX->b_type = BLT;
                break;
            // BGE
            case 0b1010:
                EX->b_type = BGE;
                break;
            // BLE
            case 0b1101:
                EX->b_type = BLE;
                break;
        }
        WB->RegWrite = false;
        // WB->MemtoReg is irrelevant
        WB->SetFlags = false;
        M->ConfirmedBranch = false;
        M->BranchIfZero = false;
        M->MemRead = false;
        M->MemWrite = false;
        // M->DataSize is irrelevant
        EX->ALUSrc = 0;
        EX->ALUOp = OP_PASSTHRU_2_S;
        init_control_stall = true;
        *frag = sign_extend_64(inst, 5, 24);
        *layout = INST_BC;
        break;


    // ---------- INST_OTHEROP ----------

    // HLT
    case 0b11010100010:

        WB->RegWrite = false;
        // WB->MemtoReg is irrelevant
        WB->SetFlags = false;
        M->ConfirmedBranch = false;
        M->BranchIfZero = false;
        M->MemRead = false;
        M->MemWrite = false;
        // M->DataSize is irrelevant
        EX->ALUSrc = 0;
        // other flags irrelevant
        CURRENT_STATE.PC += 4;
        FE_halted = true;
        break;

    // ---------- INST_INVALID ----------
    // Unsupported instruction
    default:
        assert(0); // Return an error
    }

}

// we use this for reading only for simplicity in the WB stage
void unit_Registers(
    uint64_t Read_register_1,
    uint64_t Read_register_2,
    uint64_t *Read_data_1,
    uint64_t *Read_data_2,
    uint32_t *Read_data_1_src,
    uint32_t *Read_data_2_src
) {
    *Read_data_1 = CURRENT_STATE.REGS[Read_register_1];
    *Read_data_2 = CURRENT_STATE.REGS[Read_register_2];
    *Read_data_1_src = Read_register_1;
    *Read_data_2_src = Read_register_2;
}

void unit_inst_split(
    instruction_t inst,
    uint64_t* Reg2Loc,
    uint64_t* inst_31_0,
    uint64_t* inst_31_21,
    uint64_t* inst_4_0,
    uint64_t* inst_20_16,
    uint64_t* inst_9_5
) {
    *Reg2Loc = truncator32(inst, 28, 29);
    *inst_31_0 = truncator32(inst, 0, 32);
    *inst_31_21 = truncator32(inst, 21, 32);
    *inst_4_0 = truncator32(inst, 0, 5);
    *inst_20_16 = truncator32(inst, 16, 21);
    *inst_9_5 = truncator32(inst, 5, 10);
}

void unit_mux_64(uint64_t input_true, uint64_t input_false, bool selector, uint64_t *output) {
    *output = selector ? input_true : input_false;
}

void unit_sign_extend_32_64(
    int32_t input,
    int64_t *output
) {
    *output = (int64_t)input; // C type cast works well enough
}

void unit_shift_left_2_int_64(
    int64_t input,
    int64_t *output
) {
    *output = input << 2;
}

void unit_and(
    bool input_1,
    bool input_2,
    bool *output
) {
    *output = input_1 && input_2;
}

// This function now goes through the cache
void unit_Data_memory(
    uint64_t Address,
    uint64_t Write_data,
    bool MemWrite,
    bool MemRead,
    uint64_t *Read_data,
    size_t DataSize,
    int *cycles
) {
    assert(!(MemWrite && MemRead)); // Shouldn't do both
    assert(DataSize == 8 || DataSize == 16 || DataSize == 32 || DataSize == 64);
    // If not, we probably had a corrupted instruction

    if(MemRead) {
        // on a hit, this query struct has valid data and can be used right away (remaining_cycles == 0)
        // on a miss, query.remaining_cycles will be set to 10, and a 10 cycle stall should be initiated
        // load and stores therefore take 11 cycles to complete, as the data is not written until the 11th cycle
        // when data is ready to be used, set *Read_data = query.data
        query_state_t query = cache_read_handler(d_cache, Address, DataSize / 8);
        // wait_d_cache = query.remaining_cycles > 0;
        // To make the timings correct, we set wait_d_cache
        // in cache_refresh_query_states, rather than here
        *cycles = query.remaining_cycles;
        *Read_data = query.data & ((1 << DataSize) - 1);
    } else if(MemWrite) {
        // Note that DataSize measures things in bits,
        // whereas cache_write_handler accepts sizes in bytes
        query_state_t query = cache_write_handler(d_cache, Address, DataSize / 8, Write_data);
        *cycles = query.remaining_cycles;
    } else { // Nothing to do
    }
}

void unit_forward() {
    // Forwarding rules: using a switch-case, we initialize depends_on_reg_1 and depends_on_reg_1 if the
    // instruction currently finishing the decoding stage depends on one or two of them.
    // We then check whether another instruction in a later stage will write back to the same register.
    // Crucially, this shall be done in the order of younger stages first.
    // If a collision is detected and forwarding is possible, we forward it and consider the dependency
    // resolved, and so set the corresponding depends_on_reg_x to false: any other possible collisions
    // will only involve outdated data, and as we already have younger correct data forwarded we're fine.
    // Or, if depends_on_reg_x remains true but we never encounter a register collision, then we know
    // we have no dependency issue whatsoever, and it's also fine.
    // We need a stall only if we have a collision when the dependency hasn't been resolved (set to false).
    bool need_stall = false;
    uint64_t potential_reg_1 = pipe_reg_DE_EX.Read_data_1_src;
    uint64_t potential_reg_2 = pipe_reg_DE_EX.Read_data_2_src;
    instruction_layout_t layout = pipe_reg_DE_EX.inst_layout;
    bool depends_on_reg_1 = false;
    bool depends_on_reg_2 = false;

    switch(layout) {
    case INST_NOP:
    case INST_IM:
    case INST_BC:
    case INST_B:
        depends_on_reg_1 = false;
        depends_on_reg_2 = false;
        break;
    case INST_I:
    case INST_BR:
        depends_on_reg_1 = true;
        depends_on_reg_2 = false;
        break;
    case INST_R:
        depends_on_reg_1 = true;
        depends_on_reg_2 = true;
        break;
    case INST_CB:
        depends_on_reg_1 = false;
        depends_on_reg_2 = true;
        break;
    case INST_D: // Have to determine if we have Load or Store
        depends_on_reg_1 = true;
        if(pipe_reg_DE_EX.M.MemRead) // Load; doesn't depend
            depends_on_reg_2 = false;
        else // Store will need to use the existing value in the reg
            depends_on_reg_2 = true;
        break;
    }

    // X31 is not a real register
    if(potential_reg_1 == 31)
        depends_on_reg_1 = false;
    if(potential_reg_2 == 31)
        depends_on_reg_2 = false;

    // Check for EX_MEM interface for collisions/forwarding oppotunities first
    // The initialization here assumes the sojourning instruction in EX_MEM *does*
    // write to REGS[Instruction_4_0]. It need not be the case depending on the exact inst.
    bool collision_reg_1 = (potential_reg_1 == pipe_reg_EX_MEM.Instruction_4_0) && depends_on_reg_1;
    bool collision_reg_2 = (potential_reg_2 == pipe_reg_EX_MEM.Instruction_4_0) && depends_on_reg_2;

    collision_reg_1 &= init_EX_MEM; // No collision possible if no instruction's there yet
    collision_reg_2 &= init_EX_MEM;

    if(pipe_reg_EX_MEM.inst_type == INST_DATAMOV && (pipe_reg_EX_MEM.WB.MemtoReg) && pipe_reg_EX_MEM.WB.RegWrite) { // LDUR*; result not ready at EX_MEM interface
        if(collision_reg_1 || collision_reg_2)
            need_stall = true;
    }
    else if((pipe_reg_EX_MEM.inst_type == INST_DATAMOV && (!pipe_reg_EX_MEM.WB.MemtoReg) && pipe_reg_EX_MEM.WB.RegWrite) // MOV; result is ready as pipe_reg_EX_MEM.ALUresult
         || (pipe_reg_EX_MEM.inst_type == INST_OPERATE)) { // Arithmetic instruction; result is ready as pipe_reg_EX_MEM.ALUresult
        if(collision_reg_1) {
            pipe_reg_DE_EX.Read_data_1 = pipe_reg_EX_MEM.ALUresult;
            depends_on_reg_1 = false;
        }
        if(collision_reg_2) {
            pipe_reg_DE_EX.Read_data_2 = pipe_reg_EX_MEM.ALUresult;
            depends_on_reg_2 = false;
        }
    }

    // Now check the MEM_WB interface
    collision_reg_1 = (potential_reg_1 == pipe_reg_MEM_WB.Instruction_4_0) && depends_on_reg_1 && init_MEM_WB;
    collision_reg_2 = (potential_reg_2 == pipe_reg_MEM_WB.Instruction_4_0) && depends_on_reg_2 && init_MEM_WB;
    if(pipe_reg_MEM_WB.inst_type == INST_DATAMOV && (pipe_reg_MEM_WB.WB.MemtoReg) && pipe_reg_MEM_WB.WB.RegWrite) { // LDUR*; result is ready as pipe_reg_MEM_WB.Read_data
        if(collision_reg_1) {
            pipe_reg_DE_EX.Read_data_1 = pipe_reg_MEM_WB.Read_data;
            depends_on_reg_1 = false;
        }
        if(collision_reg_2) {
            pipe_reg_DE_EX.Read_data_2 = pipe_reg_MEM_WB.Read_data;
            depends_on_reg_2 = false;
        }
    }
    else if((pipe_reg_MEM_WB.inst_type == INST_DATAMOV && (!pipe_reg_MEM_WB.WB.MemtoReg) && pipe_reg_MEM_WB.WB.RegWrite) // MOV*; result is ready as pipe_reg_MEM_WB.ALUresult
    || (pipe_reg_MEM_WB.inst_type == INST_OPERATE)) { // Arithmetic instruction; result is ready as pipe_reg_MEM_WB.ALUresult
        if(collision_reg_1) {
            pipe_reg_DE_EX.Read_data_1 = pipe_reg_MEM_WB.ALUresult;
            depends_on_reg_1 = false;
        }
        if(collision_reg_2) {
            pipe_reg_DE_EX.Read_data_2 = pipe_reg_MEM_WB.ALUresult;
            depends_on_reg_2 = false;
        }
    }

    if(need_stall)
        data_stalled = true;
    else
        data_stalled = false;

    // Now forward flags for use in the upcoming ex stage
    bool awaiting_flags = true; // We might also check if we really need the flags,
                                // i.e., if it's not a conditional b then we could
                                // set this to false right here. But for now we don't
                                // have to; it doesn't harm to have the flags forwarded.
    // Again, the order here matters. Younger results come first.
    if(pipe_reg_EX_MEM.WB.SetFlags && awaiting_flags) {
        set_flags(pipe_reg_EX_MEM.ALUresult, &pipe_reg_DE_EX.State);
        awaiting_flags = false;
    }
    // We don't need to manually handle the pipe_reg_MEM_WB contents,
    // because if the upcoming WB stage needs to set flags, it will
    // do so itself in-place.
}

void pipe_init()
{
    memset(&CURRENT_STATE, 0, sizeof(CPU_State));
    CURRENT_STATE.PC = 0x00400000;
    bp_init();
    cache_init_all();
}

void pipe_cycle()
{
    // print_bp_data();
    // FILE *fp = fopen(DEBUGGING_LOG, "a");
    // fprintf(fp, "\n----- Starting cycle %d -----\n\n", stat_cycles+1);
    // fclose(fp);
    unit_forward();
	pipe_stage_wb();
	pipe_stage_mem();
    // print_pipe_reg_MEM_WB();
	pipe_stage_execute();
    // print_pipe_reg_EX_MEM();
	pipe_stage_decode();
    // print_pipe_reg_DE_EX();
	pipe_stage_fetch();
    cache_refresh_query_states();
    // print_pipe_reg_IF_DE();
    // fp = fopen(DEBUGGING_LOG, "a");
    // fprintf(fp, "\n----- Ending cycle %d -----\n\n", stat_cycles+1);
    // fclose(fp);
}

void pipe_stage_wb()
{
    if (!init_MEM_WB)
        return;

    if (MEM_halted) {
        // cache_destroy_all();
        RUN_BIT = 0; // fully stops simulator
    }

    uint64_t WriteData;
    unit_mux_64(pipe_reg_MEM_WB.Read_data, pipe_reg_MEM_WB.ALUresult,
                pipe_reg_MEM_WB.WB.MemtoReg, &WriteData);

    if (pipe_reg_MEM_WB.WB.RegWrite) {
        uint32_t wb_reg = pipe_reg_MEM_WB.Instruction_4_0;
        if(wb_reg != 31)
            CURRENT_STATE.REGS[wb_reg] = WriteData;
    }

    // flags must be forwarded to EX as well
    if (pipe_reg_MEM_WB.WB.SetFlags)
        set_flags(WriteData, &CURRENT_STATE);

    if((pipe_reg_MEM_WB.inst_type != INST_DBUBBLE) &&
       (pipe_reg_MEM_WB.inst_type != INST_CBUBBLE) &&
       (pipe_reg_MEM_WB.inst_type != INST_MEMBUBBLE)) {
        stat_inst_retire++;
    }

}

void pipe_stage_mem()
{
    /*
     * Note about stalling on a data cache miss:
     * If a load/store misses in the cache in this cycle, the pipeline is frozen starting from the MEM stage
     * onwards. In other words, the WB in this cycle should still be completed, which is the last stage to
     * operate as normal before the pipeline is stalled for 10 cycles
     */

    int remaining_cycles;
    static pipe_reg_EX_MEM_t before_stall_backup;
    pipe_reg_EX_MEM_t temp_backup = pipe_reg_EX_MEM;

    if(wait_d_cache) {
        pipe_reg_EX_MEM = before_stall_backup;
        printf("Cycle %d: Recovering %s with addr=%lx, data=%lu\n", stat_cycles+1, (pipe_reg_EX_MEM.M.MemRead?"read":"write"), pipe_reg_EX_MEM.ALUresult, pipe_reg_EX_MEM.Read_data_2);
    }

    if (EX_halted)
        MEM_halted = true;

    if (!init_EX_MEM || MEM_halted)
        return;

    uint64_t addr = pipe_reg_EX_MEM.ALUresult;
    uint64_t Write_data = pipe_reg_EX_MEM.Read_data_2;
    uint64_t Read_data;
    unit_Data_memory(addr, Write_data, pipe_reg_EX_MEM.M.MemWrite,
                     pipe_reg_EX_MEM.M.MemRead, &Read_data, pipe_reg_EX_MEM.M.DataSize, &remaining_cycles);
    // start stalls here on d_cache miss, instructs the upstream stages (IF, DE, EX) to freeze and return early,
    // thus preserving the data in those stages and not moving them forward while the query is being resolved
    if (remaining_cycles > 0) {
        if (remaining_cycles == 10) {
            // init stall
            printf("Init mem stall at cycle %d\n", stat_cycles+1);
            printf("Storing %s inst with addr=%lx, data=%lu\n", (pipe_reg_EX_MEM.M.MemRead?"read":"write"), pipe_reg_EX_MEM.ALUresult, Write_data);
            before_stall_backup = pipe_reg_EX_MEM;
        }
        pipe_reg_MEM_WB.inst_type = INST_MEMBUBBLE;
        pipe_reg_MEM_WB.WB.RegWrite = false;
        pipe_reg_MEM_WB.WB.SetFlags = false;
        // pipe_reg_MEM_WB.WB.MemtoReg is irrelevant

        // lets data forwarding unit know to not do anything
        pipe_reg_MEM_WB.M.ConfirmedBranch = false;
        pipe_reg_MEM_WB.M.BranchIfZero = false;
        pipe_reg_MEM_WB.M.MemRead = false;
        pipe_reg_MEM_WB.M.MemWrite = false;
        return;
    } else {
        init_MEM_WB = true;
        pipe_reg_MEM_WB.inst_type = pipe_reg_EX_MEM.inst_type;
        pipe_reg_MEM_WB.WB = pipe_reg_EX_MEM.WB;
        pipe_reg_MEM_WB.ALUresult = addr;
        pipe_reg_MEM_WB.Read_data = Read_data;
        pipe_reg_MEM_WB.Instruction_4_0 = pipe_reg_EX_MEM.Instruction_4_0;
    }

    if(pipe_reg_EX_MEM.M.MemWrite || pipe_reg_EX_MEM.M.MemRead)
        pipe_reg_EX_MEM = temp_backup;

    return;
}

void pipe_stage_execute()
{
    if (DE_halted)
        EX_halted = true;

    if (!init_DE_EX || EX_halted || wait_d_cache)
        return;

    if(data_stalled) { // A bubble should originate at this stage
        // We want to propagate a DBUBBLE to the EX/MEM interface.
        // However, create_d_bubble() will overwrite the DE/EX interface,
        // but we can't corrupt the DE/EX interface because that info will be immediately
        // needed once the stall is over.
        // It's neither desirable that we basically duplicate the definition of create_d_bubble
        // here and adapt it for the EX/MEM interface, because that way maintenance will
        // be difficult--we may modify one and forget about the other.
        // My way of doing it is to back up the DE/EX interface, and restore it.
        pipe_reg_DE_EX_t backup = pipe_reg_DE_EX;

        create_d_bubble(); // corrupts pipe_reg_DE_EX
        pipe_reg_EX_MEM.State = pipe_reg_DE_EX.State;
        pipe_reg_EX_MEM.M = pipe_reg_DE_EX.M;
        pipe_reg_EX_MEM.WB = pipe_reg_DE_EX.WB;
        pipe_reg_EX_MEM.Read_data_2 = pipe_reg_DE_EX.Read_data_2;
        pipe_reg_EX_MEM.Instruction_4_0 = pipe_reg_DE_EX.Instruction_4_0;
        pipe_reg_EX_MEM.inst_type = pipe_reg_DE_EX.inst_type;

        // restore pipe_reg_DE_EX
        pipe_reg_DE_EX = backup;
        return;
    }

    if (pipe_reg_DE_EX.inst_type == INST_CBUBBLE) {
        if(control_stalled)          // We were having a control stall. However,
            control_stalled = false; // by the time a control bubble reaches the EX stage,
                                     // the branch must have been properly resolved,
                                     // so we can unstall.
        else {}                      // If we are not in a control stall but still encounter an INST_CBUBBLE,
                                     // it would be from a previous branching inst. For the current implementation,
                                     // there's nothing to do.
        pipe_reg_EX_MEM.State = pipe_reg_DE_EX.State;
        pipe_reg_EX_MEM.M = pipe_reg_DE_EX.M;
        pipe_reg_EX_MEM.WB = pipe_reg_DE_EX.WB;
        pipe_reg_EX_MEM.Read_data_2 = pipe_reg_DE_EX.Read_data_2;
        pipe_reg_EX_MEM.Instruction_4_0 = pipe_reg_DE_EX.Instruction_4_0;
        pipe_reg_EX_MEM.inst_type = pipe_reg_DE_EX.inst_type;
        return;
    }

    if (pipe_reg_DE_EX.inst_type == INST_MEMBUBBLE) {
        // Pretty much just forward the bubble
        pipe_reg_EX_MEM.State = pipe_reg_DE_EX.State;
        pipe_reg_EX_MEM.M = pipe_reg_DE_EX.M;
        pipe_reg_EX_MEM.WB = pipe_reg_DE_EX.WB;
        pipe_reg_EX_MEM.Read_data_2 = pipe_reg_DE_EX.Read_data_2;
        pipe_reg_EX_MEM.Instruction_4_0 = pipe_reg_DE_EX.Instruction_4_0;
        pipe_reg_EX_MEM.inst_type = pipe_reg_DE_EX.inst_type;
        return;
    }

    uint64_t operand1, operand2;

    // we "forward" data from WB by using the most up-to-date values of the regs
    uint64_t reg_1_val = pipe_reg_DE_EX.Read_data_1;
    uint64_t reg_2_val = pipe_reg_DE_EX.Read_data_2;

    operand1 = reg_1_val;
    unit_mux_64(pipe_reg_DE_EX.Sign_extended_frag, reg_2_val,
                pipe_reg_DE_EX.EX.ALUSrc, &operand2);

    uint64_t ALU_result;
    bool is_zero;
    unit_ALU(pipe_reg_DE_EX.EX.ALUOp, operand1, operand2, &ALU_result, &is_zero);

    int64_t offset;
    unit_shift_left_2_int_64(pipe_reg_DE_EX.Sign_extended_frag, &offset);
    uint64_t new_pc = pipe_reg_DE_EX.State.PC + offset;

    if (pipe_reg_DE_EX.inst_type == INST_CONTROL) {
        bool to_branch = pipe_reg_DE_EX.M.ConfirmedBranch;
        bool is_conditional = false;

        if(!to_branch) {
            is_conditional = true;
            switch (pipe_reg_DE_EX.EX.b_type) {
            case CBZ:
                to_branch = pipe_reg_DE_EX.State.FLAG_Z;
                break;
            case CBNZ:
                to_branch = !pipe_reg_DE_EX.State.FLAG_Z;
                break;
            // don't need to consider these since they have ConfirmedBranch = true
            // case BR:
            // case B:
            //     to_branch = true;
            //     break;
            case BEQ:
                to_branch = pipe_reg_DE_EX.State.FLAG_Z;
                break;
            case BNE:
                to_branch = !pipe_reg_DE_EX.State.FLAG_Z;
                break;
            case BGT:
                to_branch = !(pipe_reg_DE_EX.State.FLAG_Z || pipe_reg_DE_EX.State.FLAG_N);
                break;
            case BLT:
                to_branch = pipe_reg_DE_EX.State.FLAG_N;
                break;
            case BGE:
                to_branch = !pipe_reg_DE_EX.State.FLAG_N;
                break;
            case BLE:
                to_branch = pipe_reg_DE_EX.State.FLAG_Z || pipe_reg_DE_EX.State.FLAG_N;
                break;
            default:
                assert(0);
            }
        }

        pipe_reg_IF_DE.to_squash = true;

    // FILE *fp = fopen(DEBUGGING_LOG, "a");
    // fprintf(fp, "\n to_branch=%d\n", to_branch);
    // fclose(fp);
        uint64_t frozen_pc = CURRENT_STATE.PC;
        // If we had a instruction miss and branch, that may cancel the inst miss.
        // We make a backup of the CURRENT_STATE.PC, which has been frozen since
        // the inst miss happened,

        if (to_branch == pipe_reg_DE_EX.predicted_taken) {
            control_stalled = false;
            pipe_reg_IF_DE.to_squash = false;
        }
        else if (to_branch && (!pipe_reg_DE_EX.predicted_taken)) { // "False negative"
                CURRENT_STATE.PC = new_pc;
                flush_pipeline();
        }
        else /* if (!to_branch && pipe_reg_DE_EX.predicted_taken) */ { // "False positive"
            // We predicted we should branch, but turns out we should not branch
            if (pipe_reg_DE_EX.predicted_taken) {
                /*
                Reset the PC to the inst following the branch inst, since it is currently pointing to the
                inst following the incorrectly predicted branch target
                */
                CURRENT_STATE.PC = pipe_reg_DE_EX.State.PC + 4;
                flush_pipeline();
            }
        }

        bp_update(is_conditional, to_branch, pipe_reg_DE_EX.State.PC, new_pc);

        // this is to handle canceling the pending miss in i_cache if it turns out that the pending inst is
        // not the actual target of a branch inst that was fetched earlier. here, frozen_pc is the PC that was
        // predicted after the branch inst was fetched, and we compare it to the "real" branch target that was
        // resolved in this stage and put into CURRENT_STATE.PC
        uint64_t mask = (uint64_t)-1 << LOG_BLOCK_SIZE;
        if (wait_i_cache && ((frozen_pc & mask) != (CURRENT_STATE.PC & mask))) {
            wait_i_cache = false;
            cache_cancel(i_cache, frozen_pc);
            printf("cancelling\n");
        }
    }

    init_EX_MEM = true;
    pipe_reg_EX_MEM.inst_type = pipe_reg_DE_EX.inst_type;
    pipe_reg_EX_MEM.State = pipe_reg_DE_EX.State;
    pipe_reg_EX_MEM.M = pipe_reg_DE_EX.M;
    pipe_reg_EX_MEM.WB = pipe_reg_DE_EX.WB;
    pipe_reg_EX_MEM.calculated_PC = new_pc;
    pipe_reg_EX_MEM.Read_data_2 = pipe_reg_DE_EX.Read_data_2;
    pipe_reg_EX_MEM.ALUZero = is_zero;
    pipe_reg_EX_MEM.ALUresult = ALU_result;
    pipe_reg_EX_MEM.Instruction_4_0 = pipe_reg_DE_EX.Instruction_4_0;
}

void pipe_stage_decode()
{
    if (!init_IF_DE || DE_halted || wait_d_cache) {
        return;
    }

    // This order of checking flags matters: if fetch is control_stalled,
    // the inst here should be squashed (regardless of data_stalled),
    // as it will be irrelevant (in the taken case) or repetitive (in the not taken case).
    // Also, if we are both control_stalled and wait[ing_for]_i_cache
    // then we should create a CBUBBLE rather than a MEMBUBBLE, because
    // a CBUBBLE allows the EX stage to stop the control stall.
    // However, if an inst is only data_stalled, then it should not be squashed.
    // This is because it's still a useful future inst, just stalled.
    if (pipe_reg_IF_DE.to_squash || pipe_reg_IF_DE.to_flush) {
        //assert(!pipe_reg_IF_DE.to_mem_stall); // Can't possibly happen at the same time
        create_c_bubble();
        return;
    }
    else if (pipe_reg_IF_DE.to_mem_stall) {
        //assert(!pipe_reg_IF_DE.to_squash); // Can't possibly happen at the same time
        create_mem_bubble();
        return;
    }
    else if (data_stalled) {
        // The DE/EX interface has valid content, just stalled.
        // We don't mess with it and just allow it to be picked up
        // in a later cycle.
        return;
    }

    if (pipe_reg_IF_DE.to_mem_stall) {
        create_mem_bubble();
        return;
    }

    instruction_t raw_inst = pipe_reg_IF_DE.Instruction_full;

    uint64_t inst_Reg2Loc;
    uint64_t inst_31_0;
    uint64_t inst_31_21;
    uint64_t inst_4_0;
    uint64_t inst_20_16;
    uint64_t inst_9_5;
    unit_inst_split(raw_inst, &inst_Reg2Loc, &inst_31_0, &inst_31_21, &inst_4_0, &inst_20_16, &inst_9_5);
    pipe_reg_DE_EX.Instruction_31_21 = inst_31_21;
    uint32_t Instruction_4_0; // Possibly specifies the register to wb to

    uint64_t read_reg_1, read_reg_2;
    unit_mux_64(inst_4_0, inst_20_16, inst_Reg2Loc, &read_reg_2);

    uint64_t read_data_1, read_data_2;

    unit_control(
        raw_inst,
        pipe_reg_IF_DE.State,
        &pipe_reg_DE_EX.WB,
        &pipe_reg_DE_EX.M,
        &pipe_reg_DE_EX.EX,
        &pipe_reg_DE_EX.Sign_extended_frag,
        &pipe_reg_DE_EX.inst_layout,
        &pipe_reg_DE_EX.inst_type
    );

    read_reg_1 = inst_9_5; // may be a register or just garbage
    read_reg_2 = read_reg_2; // this too

    unit_Registers(
        read_reg_1,
        read_reg_2,
        &pipe_reg_DE_EX.Read_data_1,
        &pipe_reg_DE_EX.Read_data_2,
        &pipe_reg_DE_EX.Read_data_1_src,
        &pipe_reg_DE_EX.Read_data_2_src
    );


    init_DE_EX = true;
    pipe_reg_DE_EX.State = pipe_reg_IF_DE.State;
    // pipe_reg_DE_EX.Read_data_1 has been updated
    // pipe_reg_DE_EX.Read_data_2 has been updated
    pipe_reg_DE_EX.Instruction_31_21 = inst_31_21;
    pipe_reg_DE_EX.Instruction_4_0 = inst_4_0;

    pipe_reg_DE_EX.predicted_taken = pipe_reg_IF_DE.predicted_taken;
    pipe_reg_DE_EX.predicted_pc = pipe_reg_IF_DE.predicted_pc;

    if (FE_halted) // This should come *before* the check for DE_halted
        DE_halted = true;

}

void pipe_stage_fetch()
{
    if (FE_halted || wait_d_cache) {
        return;
    }

    // As in pipe_stage_decode, this order of checking insts matters.
    if (control_stalled) {
        pipe_reg_IF_DE.to_squash = true; // As fetch is not control_stalled,
        printf("Control stalled at cycle %d, current PC=%lx\n", stat_cycles+1, CURRENT_STATE.PC);
        return;//because its fetched inst will be used or squashed the next cycle
    }
    else if (data_stalled) {
        return;
    }
    printf("Not control stalled at cycle %d, current PC=%lx\n", stat_cycles+1, CURRENT_STATE.PC);

    if (init_control_stall) {
        init_control_stall = false;
        control_stalled = true;
    }

    pipe_reg_IF_DE.Instruction_full = fetch(CURRENT_STATE.PC);
    if (wait_i_cache) {
        // init stall, need to create bubble
        pipe_reg_IF_DE.to_mem_stall = true;
        return;
    }

    init_IF_DE = true;

    pipe_reg_IF_DE.State = CURRENT_STATE;
    pipe_reg_IF_DE.to_squash = false;
    pipe_reg_IF_DE.to_flush = false;
    pipe_reg_IF_DE.to_mem_stall = false;

    // update PC to prediction
    bp_predict(CURRENT_STATE.PC, &pipe_reg_IF_DE.predicted_pc, &pipe_reg_IF_DE.predicted_taken);
    CURRENT_STATE.PC = pipe_reg_IF_DE.predicted_pc;
    printf("updated PC=%lx\n", CURRENT_STATE.PC);
}
