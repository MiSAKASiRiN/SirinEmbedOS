#include "arm_math.h"

/*
 * This project ships the CMSIS-DSP public header but not a prebuilt DSP
 * archive.  Keep the Kalman API on CMSIS matrix types/operations and provide
 * weak, allocation-free implementations for the three operations it requires.
 * A linked CMSIS-DSP archive can replace these weak definitions unchanged.
 */

__attribute__((weak)) void arm_mat_init_f32(
    arm_matrix_instance_f32 *matrix,
    uint16_t rows,
    uint16_t columns,
    float32_t *data)
{
    /* Match the upstream initialization contract: bind caller-owned storage;
     * do not allocate, clear the data, or validate the dimensions. */
    matrix->numRows = rows;
    matrix->numCols = columns;
    matrix->pData = data;
}

__attribute__((weak)) arm_status arm_mat_mult_f32(
    const arm_matrix_instance_f32 *left,
    const arm_matrix_instance_f32 *right,
    arm_matrix_instance_f32 *output)
{
    if ((left == 0) || (right == 0) || (output == 0) ||
        (left->pData == 0) || (right->pData == 0) || (output->pData == 0) ||
        (left->numCols != right->numRows) ||
        (output->numRows != left->numRows) ||
        (output->numCols != right->numCols)) {
        return ARM_MATH_SIZE_MISMATCH;
    }

    for (uint16_t row = 0U; row < output->numRows; ++row) {
        for (uint16_t column = 0U; column < output->numCols; ++column) {
            float value = 0.0f;
            for (uint16_t inner = 0U; inner < left->numCols; ++inner) {
                value += left->pData[row * left->numCols + inner] *
                         right->pData[inner * right->numCols + column];
            }
            output->pData[row * output->numCols + column] = value;
        }
    }
    return ARM_MATH_SUCCESS;
}

__attribute__((weak)) arm_status arm_mat_trans_f32(
    const arm_matrix_instance_f32 *input,
    arm_matrix_instance_f32 *output)
{
    if ((input == 0) || (output == 0) ||
        (input->pData == 0) || (output->pData == 0) ||
        (output->numRows != input->numCols) ||
        (output->numCols != input->numRows)) {
        return ARM_MATH_SIZE_MISMATCH;
    }

    for (uint16_t row = 0U; row < input->numRows; ++row) {
        for (uint16_t column = 0U; column < input->numCols; ++column) {
            output->pData[column * output->numCols + row] =
                input->pData[row * input->numCols + column];
        }
    }
    return ARM_MATH_SUCCESS;
}
