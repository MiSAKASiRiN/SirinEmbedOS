#ifndef SRNOS_MATRIX_H
#define SRNOS_MATRIX_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Row-major caller-owned storage. Bind does not allocate or clear data.
 * Multiply/transpose require correctly sized, non-overlapping output storage. */
typedef struct { uint16_t rows, columns; float *data; } SRNOS_MatrixF32;
typedef enum { SRNOS_MATRIX_OK = 0, SRNOS_MATRIX_SIZE_MISMATCH = -1 } SRNOS_MatrixStatus;
void SRNOS_MatrixInitF32(SRNOS_MatrixF32 *, uint16_t rows, uint16_t columns, float *data);
SRNOS_MatrixStatus SRNOS_MatrixMultiplyF32(const SRNOS_MatrixF32 *, const SRNOS_MatrixF32 *, SRNOS_MatrixF32 *);
SRNOS_MatrixStatus SRNOS_MatrixTransposeF32(const SRNOS_MatrixF32 *, SRNOS_MatrixF32 *);
#ifdef __cplusplus
}
#endif
#endif
