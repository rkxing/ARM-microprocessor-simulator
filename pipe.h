#ifndef _PIPE_H_
#define _PIPE_H_

#include "shell.h"
#include "stdbool.h"
#include <stddef.h>
#include <limits.h>


typedef struct CPU_State {
	/* register file state */
	int64_t REGS[ARM_REGS];
	int FLAG_N;        /* flag N */
	int FLAG_Z;        /* flag Z */

	/* program counter in fetch stage */
	uint64_t PC;

} CPU_State;

extern int RUN_BIT;
extern bool wait_i_cache;
extern bool wait_d_cache;

/* global variable -- pipeline state */
extern CPU_State CURRENT_STATE;

typedef uint32_t instruction_t;

typedef enum {
    INST_OPERATE,
    INST_DATAMOV,
    INST_CONTROL,
    INST_OTHEROP, // An example of this is HLT or NOP
    INST_DBUBBLE,
    INST_CBUBBLE,
    INST_MEMBUBBLE,
    INST_INVALID
} instruction_type_t;

typedef enum {
    INST_R,
    INST_I,
    INST_D,
    INST_B,
    INST_CB,
    INST_IM, // Not sure if it's really IM or IW on the LEGv8 reference page
    INST_BR, // Doesn't really fit in type R
    INST_BC, // For B.cond; similar to INST_B in that it's not involved in any dependency
    INST_NOP // e.g. HLT; assumed not to be involved in any dependency
} instruction_layout_t;

typedef enum {
    OP_ADD,
    OP_ADDS,
    OP_AND,
    OP_ANDS,
    //OP_CMP, // CMP is defined to be an alias of SUBS when dest.==XZR
    OP_EOR,
    OP_ORR,
    OP_LSL,
    OP_LSR,
    OP_SUB,
    OP_SUBS,
    OP_MUL,
    OP_PASSTHRU_2_S,  // Simply forwards the second operand (i.e. the one
                      // from a mux rather than the one directly passed
                      // from Read_data_1) to ALUresult, and set ALUZero
                      // accordingly; used by some branch insts.
    OP_NOT_2_S // Returns 1 for 0 and 0 for anything nonzero for ALUresult,
                // and set ALUZero according to ALUresult.
} mop_operate_type_t;

typedef enum {
    CBZ,
    CBNZ,
    BR,
    B,
    BEQ,
    BNE,
    BGT,
    BLT,
    BGE,
    BLE
} branch_type;

typedef struct {
    bool RegWrite;
    bool MemtoReg;
    bool SetFlags;
} interface_WB;

typedef struct {
    bool ConfirmedBranch; // Unconditional or B.cond (e.g. BEQ but not CBZ)
                          // where the flags are checked to be satisfying
    bool BranchIfZero; // Set to false for unconditional jumps
    bool MemRead;
    bool MemWrite;
    size_t DataSize;
} interface_M;

typedef struct {
    bool ALUSrc;
    mop_operate_type_t ALUOp;
    branch_type b_type;
} interface_EX;


typedef struct {
    CPU_State State;
    uint32_t Instruction_full;
    bool to_squash;
    bool to_flush;
    bool to_mem_stall;
    bool predicted_taken;
    uint64_t predicted_pc;
} pipe_reg_IF_DE_t;
extern pipe_reg_IF_DE_t pipe_reg_IF_DE;

typedef struct {
    interface_EX EX;
    interface_M M;
    interface_WB WB;
    CPU_State State;
    instruction_type_t inst_type; // For forwarding unit to know if output should be forwarded
        // For instance, ALU_result should be forwarded for arithmetic (INST_OPERATE) insts,
        // but not for DATAMOV insts (these are addresses, not the desired value yet!)
    instruction_layout_t inst_layout;
    uint32_t Read_data_1_src; // The index of the reg from which Read_data_1 is read
    uint32_t Read_data_2_src; // The index of the reg from which Read_data_1 is read
    uint64_t Read_data_1;
    uint64_t Read_data_2;
    int64_t Sign_extended_frag; // Possibly used as offset for branching
                                // A fragment whose position depends on opcode
    uint32_t Instruction_31_21; // Possibly used for ALU control
    uint32_t Instruction_4_0; // Possibly specifies the register to wb to
    bool predicted_taken;
    uint64_t predicted_pc;
} pipe_reg_DE_EX_t;
extern pipe_reg_DE_EX_t pipe_reg_DE_EX;

typedef struct {
    //interface_EX EX; // Consumed
    interface_M M;
    interface_WB WB;
    CPU_State State; // Used, but not consumed
    instruction_type_t inst_type;
    uint64_t calculated_PC;
    //uint64_t Read_data_1; // Consumed
    uint64_t Read_data_2; // Used once but still needs to be passed on
    bool ALUZero;
    uint64_t ALUresult;
    //uint32_t Instruction_31_0; // Consumed
    //uint32_t Instruction_31_21; // Consumed
    uint32_t Instruction_4_0; // Possibly specifies the register to wb to
} pipe_reg_EX_MEM_t;
extern pipe_reg_EX_MEM_t pipe_reg_EX_MEM;

typedef struct {
    instruction_type_t inst_type;
    interface_M M; // Consumed, but has to be kept for the forwarding unit
    interface_WB WB;
    //CPU_State State; // Consumed
    //uint64_t calculated_PC; // Consumed
    //uint64_t Read_data_2; // Consumed
    //bool ALUZero; // Consumed
    uint64_t ALUresult; // Used once but still needs to be passed on
    uint64_t Read_data; // Data read from RAM (NOT registers)
    uint32_t Instruction_4_0; // Possibly specifies the register to wb to
} pipe_reg_MEM_WB_t;
extern pipe_reg_MEM_WB_t pipe_reg_MEM_WB;

uint64_t sign_extend(uint32_t data, size_t begin, size_t end);

void set_flags(uint64_t result, CPU_State *state);

int64_t sign_extend_64(uint32_t data, size_t begin, size_t end);

void unit_control_hazard_stall(instruction_t *inst);

void unit_control_r_format(
    interface_WB *WB,
    interface_M *M,
    interface_EX *EX,
    instruction_layout_t *layout
);

void unit_control_i_format(
    instruction_t inst,
    interface_WB *WB,
    interface_M *M,
    interface_EX *EX,
    int64_t *frag,
    instruction_layout_t *layout
);

void unit_control_d_format(
    instruction_t inst,
    interface_WB *WB,
    interface_M *M,
    interface_EX *EX,
    int64_t *frag,
    instruction_layout_t *layout
);

void unit_control_nop(); // Will be used for both HLT and for the forwarding unit
// so we just make it use global vars instead of passing things to it.

void unit_ALU(mop_operate_type_t op_type, uint64_t operand1, uint64_t operand2, uint64_t* ALU_result, bool* is_zero);

/* called during simulator startup */
void pipe_init();

/* this function calls the others */
void pipe_cycle();

// CONVENTION: we don't make the component blocks return;
// rather, they should accept pointers and are responsible
// for writing values themselves.

void unit_Registers(
    uint64_t Read_register_1,
    uint64_t Read_register_2,
    uint64_t *Read_data_1,
    uint64_t *Read_data_2,
    uint32_t *Read_data_1_src,
    uint32_t *Read_data_2_src
);

void unit_control(
    uint32_t inst,
    CPU_State state,
    interface_WB *WB,
    interface_M *M,
    interface_EX *EX,
    int64_t *frag,
    instruction_layout_t *layout,
    instruction_type_t *type
);

void unit_ALU_control(
    uint32_t Instruction_31_21,
    uint32_t ALUOp,
    uint32_t *output
);

void unit_inst_split(
    instruction_t inst,
    uint64_t* Reg2Loc,
    uint64_t* inst_31_0,
    uint64_t* inst_31_21,
    uint64_t* inst_4_0,
    uint64_t* inst_20_16,
    uint64_t* inst_9_5
);

void unit_mux_64(
    uint64_t input_true,
    uint64_t input_false,
    bool selector,
    uint64_t *output
);

void unit_sign_extend_32_64(
    int32_t input,
    int64_t *output
);

void unit_shift_left_2_int_64(
    int64_t input,
    int64_t *output
);

void unit_and(
    bool input_1,
    bool input_2,
    bool *output
);

void unit_Data_memory(
    uint64_t Address,
    uint64_t Write_data,
    bool MemWrite,
    bool MemRead,
    uint64_t *Read_data,
    size_t DataSize,
    int *cycles
);

void unit_split(instruction_t inst);

// A "global" unit which will directly access variables in the pipe regs
// rather than take pointers
void unit_forward();

/* each of these functions implements one stage of the pipeline */
void pipe_stage_fetch();
void pipe_stage_decode();
void pipe_stage_execute();
void pipe_stage_mem();
void pipe_stage_wb();

#endif
