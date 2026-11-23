#ifndef QUANTAPDF_RESOURCE_MASK_CALCULATOR_H
#define QUANTAPDF_RESOURCE_MASK_CALCULATOR_H

#include <stdint.h>

/* The parser and evaluator share these opcodes. Conditional blocks compile
 * to forward jumps, with no procedure values on the operand stack.
 * Table 42 operators are grouped by unary, binary, then stack dispatch. */
#define QP_CALCULATOR_OPERATORS(X) \
    X(abs) X(ceiling) X(cos) X(cvi) X(cvr) X(floor) X(ln) X(log) X(neg) \
    X(not) X(round) X(sin) X(sqrt) X(truncate) \
    X(add) X(and) X(atan) X(bitshift) X(div) X(eq) X(exp) X(ge) X(gt) \
    X(idiv) X(le) X(lt) X(mod) X(mul) X(ne) X(or) X(sub) X(xor) \
    X(copy) X(dup) X(exch) X(index) X(pop) X(roll)

typedef enum qp_calculator_opcode {
    QP_CALC_PUSH_INT,
    QP_CALC_PUSH_REAL,
    QP_CALC_PUSH_BOOL,
    QP_CALC_JUMP_IF_FALSE,
    QP_CALC_JUMP,
#define QP_CALC_ENUM(name) QP_CALC_OP_##name,
    QP_CALCULATOR_OPERATORS(QP_CALC_ENUM)
#undef QP_CALC_ENUM
} qp_calculator_opcode;

typedef struct qp_calculator_instruction {
    double value;
    uint32_t opcode, target;
} qp_calculator_instruction;

#endif
