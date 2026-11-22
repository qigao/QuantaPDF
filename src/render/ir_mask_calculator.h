#ifndef QUANTAPDF_RENDER_IR_MASK_CALCULATOR_H
#define QUANTAPDF_RENDER_IR_MASK_CALCULATOR_H

#include "resource/mask_calculator.h"
#include <quantapdf/quantapdf.h>
#include <math.h>
#include <string.h>

typedef struct qp_calc_value {
    double value;
    uint32_t type;
} qp_calc_value;

enum { QP_CALCULATOR_STACK_SIZE = 100 };

static inline int qp_calc_fits_integer(double value)
{
    return value >= INT32_MIN && value <= INT32_MAX;
}

static inline double qp_calc_signed_bits(uint32_t bits)
{
    return bits <= INT32_MAX ? (double)bits : (double)bits - 4294967296.0;
}

static inline int qp_calc_charge(size_t *work, size_t cost, size_t cap)
{
    if (cost > cap - *work) return 0;
    *work += cost;
    return 1;
}

static inline int qp_calc_unary(uint32_t opcode, qp_calc_value *operand)
{
    const double x = operand->value;
    const double radians_per_degree = 0.01745329251994329577;
    double result;
    uint32_t type = operand->type;

    if (opcode == QP_CALC_OP_not) {
        if (type == QP_CALC_PUSH_REAL) return 0;
        result = type == QP_CALC_PUSH_BOOL ? (double)(x == 0.0)
            : qp_calc_signed_bits(~(uint32_t)(int32_t)x);
    } else {
        if (type == QP_CALC_PUSH_BOOL) return 0;
        switch (opcode) {
        case QP_CALC_OP_abs: result = fabs(x); break;
        case QP_CALC_OP_ceiling: result = ceil(x); break;
        case QP_CALC_OP_cos:
            result = cos(fmod(x, 360.0) * radians_per_degree);
            type = QP_CALC_PUSH_REAL;
            break;
        case QP_CALC_OP_cvi:
            result = trunc(x);
            if (!qp_calc_fits_integer(result)) return 0;
            type = QP_CALC_PUSH_INT;
            break;
        case QP_CALC_OP_cvr: result = x; type = QP_CALC_PUSH_REAL; break;
        case QP_CALC_OP_floor: result = floor(x); break;
        case QP_CALC_OP_ln: result = log(x); type = QP_CALC_PUSH_REAL; break;
        case QP_CALC_OP_log: result = log10(x); type = QP_CALC_PUSH_REAL; break;
        case QP_CALC_OP_neg: result = -x; break;
        case QP_CALC_OP_round:
            result = floor(x);
            if (x - result >= 0.5) result += 1.0;
            break;
        case QP_CALC_OP_sin:
            result = sin(fmod(x, 360.0) * radians_per_degree);
            type = QP_CALC_PUSH_REAL;
            break;
        case QP_CALC_OP_sqrt: result = sqrt(x); type = QP_CALC_PUSH_REAL; break;
        case QP_CALC_OP_truncate: result = trunc(x); break;
        default: return 0;
        }
    }
    if (!isfinite(result)) return 0;
    if (type == QP_CALC_PUSH_INT && !qp_calc_fits_integer(result))
        type = QP_CALC_PUSH_REAL;
    *operand = (qp_calc_value){result, type};
    return 1;
}

static inline int qp_calc_binary(uint32_t opcode, qp_calc_value *left, qp_calc_value right)
{
    const double a = left->value, b = right.value;
    const int integers = left->type == QP_CALC_PUSH_INT && right.type == QP_CALC_PUSH_INT;
    const int a_bool = left->type == QP_CALC_PUSH_BOOL, b_bool = right.type == QP_CALC_PUSH_BOOL;
    double result;
    uint32_t type = QP_CALC_PUSH_REAL;

    if (opcode == QP_CALC_OP_eq || opcode == QP_CALC_OP_ne) {
        const int equal = a_bool == b_bool && a == b;
        *left = (qp_calc_value){(double)(opcode == QP_CALC_OP_eq ? equal : !equal), QP_CALC_PUSH_BOOL};
        return 1;
    }
    if (opcode == QP_CALC_OP_and || opcode == QP_CALC_OP_or || opcode == QP_CALC_OP_xor) {
        if (!integers && !(a_bool && b_bool)) return 0;
        const uint32_t x = (uint32_t)(int32_t)a, y = (uint32_t)(int32_t)b;
        uint32_t bits = x ^ y;
        if (opcode == QP_CALC_OP_and) bits = x & y;
        if (opcode == QP_CALC_OP_or) bits = x | y;
        *left = (qp_calc_value){qp_calc_signed_bits(bits), left->type};
        return 1;
    }
    if (a_bool || b_bool) return 0;
    switch (opcode) {
    case QP_CALC_OP_add: result = a + b; break;
    case QP_CALC_OP_sub: result = a - b; break;
    case QP_CALC_OP_mul: result = a * b; break;
    case QP_CALC_OP_atan:
        if (a == 0.0 && b == 0.0) return 0;
        result = atan2(a, b) * 57.29577951308232088;
        if (result < 0.0) result += 360.0;
        break;
    case QP_CALC_OP_div:
        if (b == 0.0) return 0;
        result = a / b;
        break;
    case QP_CALC_OP_exp: result = pow(a, b); break;
    case QP_CALC_OP_idiv:
    case QP_CALC_OP_mod: {
        if (!integers || b == 0.0) return 0;
        const int64_t x = (int64_t)a, y = (int64_t)b;
        result = (double)(opcode == QP_CALC_OP_idiv ? x / y : x % y);
        if (!qp_calc_fits_integer(result)) return 0;
        type = QP_CALC_PUSH_INT;
        break;
    }
    case QP_CALC_OP_bitshift: {
        if (!integers) return 0;
        const uint32_t x = (uint32_t)(int32_t)a;
        const int32_t shift = (int32_t)b;
        uint32_t bits = 0u;
        if (shift >= 0 && shift < 32) bits = x << (unsigned)shift;
        if (shift < 0 && shift > -32) bits = x >> (unsigned)-shift;
        result = qp_calc_signed_bits(bits);
        type = QP_CALC_PUSH_INT;
        break;
    }
    case QP_CALC_OP_ge: result = (double)(a >= b); type = QP_CALC_PUSH_BOOL; break;
    case QP_CALC_OP_gt: result = (double)(a > b); type = QP_CALC_PUSH_BOOL; break;
    case QP_CALC_OP_le: result = (double)(a <= b); type = QP_CALC_PUSH_BOOL; break;
    case QP_CALC_OP_lt: result = (double)(a < b); type = QP_CALC_PUSH_BOOL; break;
    default: return 0;
    }
    if (!isfinite(result)) return 0;
    if (integers && qp_calc_fits_integer(result) &&
        (opcode == QP_CALC_OP_add || opcode == QP_CALC_OP_sub || opcode == QP_CALC_OP_mul))
        type = QP_CALC_PUSH_INT;
    *left = (qp_calc_value){result, type};
    return 1;
}

static inline quantapdf_status qp_calc_stack(uint32_t opcode, qp_calc_value *stack,
    size_t *size, size_t *work, size_t cap)
{
    size_t n = *size;
    if (n == 0u) return QUANTAPDF_ERROR_FORMAT;
    if (opcode == QP_CALC_OP_dup) {
        if (n == QP_CALCULATOR_STACK_SIZE) return QUANTAPDF_ERROR_FORMAT;
        stack[n] = stack[n - 1u];
        ++n;
    } else if (opcode == QP_CALC_OP_exch) {
        if (n < 2u) return QUANTAPDF_ERROR_FORMAT;
        const qp_calc_value top = stack[n - 1u];
        stack[n - 1u] = stack[n - 2u];
        stack[n - 2u] = top;
    } else if (opcode == QP_CALC_OP_pop) {
        --n;
    } else {
        const qp_calc_value operand = stack[--n];
        if (operand.type != QP_CALC_PUSH_INT) return QUANTAPDF_ERROR_FORMAT;
        if (opcode == QP_CALC_OP_roll) {
            if (n == 0u) return QUANTAPDF_ERROR_FORMAT;
            const qp_calc_value count = stack[--n];
            if (count.type != QP_CALC_PUSH_INT || count.value < 0.0 || count.value > (double)n)
                return QUANTAPDF_ERROR_FORMAT;
            const int32_t length = (int32_t)count.value;
            if (length != 0) {
                int32_t shift = (int32_t)operand.value % length;
                if (shift < 0) shift += length;
                if (shift != 0) {
                    if (!qp_calc_charge(work, (size_t)length, cap)) return QUANTAPDF_ERROR_UNSUPPORTED;
                    qp_calc_value rotated[QP_CALCULATOR_STACK_SIZE];
                    qp_calc_value *tail = stack + n - (size_t)length;
                    for (int32_t i = 0; i < length; ++i)
                        rotated[(i + shift) % length] = tail[i];
                    memcpy(tail, rotated, (size_t)length * sizeof(*stack));
                }
            }
        } else {
            if (operand.value < 0.0 || operand.value > (double)n) return QUANTAPDF_ERROR_FORMAT;
            const size_t count = (size_t)operand.value;
            if (opcode == QP_CALC_OP_index) {
                if (count == n) return QUANTAPDF_ERROR_FORMAT;
                stack[n] = stack[n - count - 1u];
                ++n;
            } else {
                if (count > QP_CALCULATOR_STACK_SIZE - n) return QUANTAPDF_ERROR_FORMAT;
                if (!qp_calc_charge(work, count, cap)) return QUANTAPDF_ERROR_UNSUPPORTED;
                memcpy(stack + n, stack + n - count, count * sizeof(*stack));
                n += count;
            }
        }
    }
    *size = n;
    return QUANTAPDF_OK;
}

static inline quantapdf_status qp_mask_calculate(const unsigned char *code, size_t length,
    double input, size_t *work, size_t cap, double *out)
{
    qp_calc_value stack[QP_CALCULATOR_STACK_SIZE];
    size_t size = 1u, pc = 0u;
    stack[0] = (qp_calc_value){input, QP_CALC_PUSH_REAL};
    while (pc < length) {
        if (!qp_calc_charge(work, 1u, cap)) return QUANTAPDF_ERROR_UNSUPPORTED;
        qp_calculator_instruction instruction;
        memcpy(&instruction, code + pc++ * sizeof(instruction), sizeof(instruction));
        const uint32_t opcode = instruction.opcode;
        if (opcode <= QP_CALC_PUSH_BOOL) {
            if (size == QP_CALCULATOR_STACK_SIZE) return QUANTAPDF_ERROR_FORMAT;
            stack[size++] = (qp_calc_value){instruction.value, opcode};
        } else if (opcode == QP_CALC_JUMP) {
            pc = instruction.target;
        } else if (opcode == QP_CALC_JUMP_IF_FALSE) {
            if (size == 0u || stack[size - 1u].type != QP_CALC_PUSH_BOOL)
                return QUANTAPDF_ERROR_FORMAT;
            if (stack[--size].value == 0.0) pc = instruction.target;
        } else if (opcode >= QP_CALC_OP_copy) {
            const quantapdf_status status = qp_calc_stack(opcode, stack, &size, work, cap);
            if (status != QUANTAPDF_OK) return status;
        } else if (opcode <= QP_CALC_OP_truncate) {
            if (size == 0u || !qp_calc_unary(opcode, &stack[size - 1u])) return QUANTAPDF_ERROR_FORMAT;
        } else {
            if (size < 2u || !qp_calc_binary(opcode, &stack[size - 2u], stack[size - 1u]))
                return QUANTAPDF_ERROR_FORMAT;
            --size;
        }
    }
    if (size != 1u || stack[0].type == QP_CALC_PUSH_BOOL) return QUANTAPDF_ERROR_FORMAT;
    *out = stack[0].value;
    return QUANTAPDF_OK;
}

#endif
