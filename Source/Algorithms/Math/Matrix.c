#include "SirinEmbedOS/Algorithms/Math/Matrix.h"

/* Allocation-free matrix backend; storage belongs to the caller. */

void SRNOS_MatrixInitF32(
    SRNOS_MatrixF32 *matrix,
    uint16_t rows,
    uint16_t columns,
    float *data)
{
    /* Match the upstream initialization contract: bind caller-owned storage;
     * do not allocate, clear the data, or validate the dimensions. */
    matrix->rows = rows;
    matrix->columns = columns;
    matrix->data = data;
}

SRNOS_MatrixStatus SRNOS_MatrixMultiplyF32(
    const SRNOS_MatrixF32 *left,
    const SRNOS_MatrixF32 *right,
    SRNOS_MatrixF32 *output)
{
    if ((left == 0) || (right == 0) || (output == 0) ||
        (left->data == 0) || (right->data == 0) || (output->data == 0) ||
        (left->columns != right->rows) ||
        (output->rows != left->rows) ||
        (output->columns != right->columns)) {
        return SRNOS_MATRIX_SIZE_MISMATCH;
    }

    for (uint16_t row = 0U; row < output->rows; ++row) {
        for (uint16_t column = 0U; column < output->columns; ++column) {
            float value = 0.0f;
            for (uint16_t inner = 0U; inner < left->columns; ++inner) {
                value += left->data[row * left->columns + inner] *
                         right->data[inner * right->columns + column];
            }
            output->data[row * output->columns + column] = value;
        }
    }
    return SRNOS_MATRIX_OK;
}

SRNOS_MatrixStatus SRNOS_MatrixTransposeF32(
    const SRNOS_MatrixF32 *input,
    SRNOS_MatrixF32 *output)
{
    if ((input == 0) || (output == 0) ||
        (input->data == 0) || (output->data == 0) ||
        (output->rows != input->columns) ||
        (output->columns != input->rows)) {
        return SRNOS_MATRIX_SIZE_MISMATCH;
    }

    for (uint16_t row = 0U; row < input->rows; ++row) {
        for (uint16_t column = 0U; column < input->columns; ++column) {
            output->data[column * output->columns + row] =
                input->data[row * input->columns + column];
        }
    }
    return SRNOS_MATRIX_OK;
}
