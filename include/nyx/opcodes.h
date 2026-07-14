#ifndef NYX_OPCODES_H
#define NYX_OPCODES_H

/*
 * One case in the VM's dispatch loop (vm.c) per opcode here. Keep this list
 * and debug.c's disassembleInstruction() in sync -- the disassembler is
 * driven off this enum by array index (see OPCODE_NAMES in debug.c), so an
 * opcode added here without a matching debug.c entry will misprint every
 * opcode after it when you use `nyx --disasm`.
 */
typedef enum {
    OP_CONSTANT, /* operand: 1-byte constant pool index */
    OP_NIL,
    OP_TRUE,
    OP_FALSE,
    OP_POP,
    OP_DUP,

    OP_GET_LOCAL,   /* operand: 1-byte stack slot */
    OP_SET_LOCAL,   /* operand: 1-byte stack slot */
    OP_GET_GLOBAL,  /* operand: 1-byte constant pool index (name) */
    OP_DEFINE_GLOBAL,
    OP_SET_GLOBAL,
    OP_GET_UPVALUE, /* operand: 1-byte upvalue index */
    OP_SET_UPVALUE,

    OP_GET_INDEX, /* pops (container, index), pushes value */
    OP_SET_INDEX, /* pops (container, index, value), pushes value */

    OP_EQUAL,
    OP_GREATER,
    OP_LESS,
    OP_ADD,
    OP_SUBTRACT,
    OP_MULTIPLY,
    OP_DIVIDE,
    OP_MODULO,
    OP_NOT,
    OP_NEGATE,

    OP_JUMP,           /* operand: 2-byte forward offset */
    OP_JUMP_IF_FALSE,  /* operand: 2-byte forward offset; does not pop */
    OP_LOOP,           /* operand: 2-byte backward offset */

    OP_CALL,     /* operand: 1-byte argument count */
    OP_CLOSURE,  /* operand: constant index, then per-upvalue (isLocal, index) pairs */
    OP_CLOSE_UPVALUE,
    OP_RETURN,

    OP_ARRAY, /* operand: 1-byte element count; pops N values, pushes array */
    OP_MAP,   /* operand: 1-byte pair count; pops 2N values, pushes map */

    OP_COUNT /* not a real opcode; keeps debug.c's name table honest */
} OpCode;

#endif
