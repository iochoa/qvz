#ifndef _CODEBOOK_H_
#define _CODEBOOK_H_
/**
 * Functions and definitions relating to reading codebooks from files, used
 * for both the encoder and decoder code
 */

#include "util.h"

#include <stdint.h>
#include <string.h>

#include "well.h"
#include "pmf.h"
#include "distortion.h"
#include "quantizer.h"
#include "lines.h"

/**
 * Stores an array of conditional PMFs for the current column given the previous
 * column. PMF pointers are stored in a flat array so don't try to find the PMF you
 * want directly--use the accessor
 */
struct cond_pmf_list_t {
	uint32_t columns;
	const struct alphabet_t *alphabet;
	struct pmf_t **pmfs;
	struct pmf_list_t *marginal_pmfs;
};

/**
 * Stores an array of quantizer pointers for the column for all possible left context
 * values. Unused ones are left as null pointers. This is also stored as a flat array
 * so the accessor must be used to look up the correct quantizer
 * The dreaded triple pointer is used to store an array of (different length) arrays
 * of pointers to quantizers
 */
struct cond_quantizer_list_t {
	uint32_t columns;
	struct alphabet_t **input_alphabets;
	struct quantizer_t ***q;
	double **ratio;				// Raw ratio
	uint8_t **qratio;			// Quantized ratio
	struct well_state_t well;
};

// Memory management
struct cond_pmf_list_t *alloc_conditional_pmf_list(const struct alphabet_t *alphabet, uint32_t columns);
struct cond_quantizer_list_t *alloc_conditional_quantizer_list(uint32_t columns);
void free_cond_pmf_list(struct cond_pmf_list_t *);
void free_cond_quantizer_list(struct cond_quantizer_list_t *);

// Per-column initializer for conditional quantizer list
void cond_quantizer_init_column(struct cond_quantizer_list_t *list, uint32_t column, const struct alphabet_t *input_union);

// Accessors
struct pmf_t *get_cond_pmf(struct cond_pmf_list_t *list, uint32_t column, symbol_t prev);
struct quantizer_t *get_cond_quantizer_indexed(struct cond_quantizer_list_t *list, uint32_t column, uint32_t index);
struct quantizer_t *get_cond_quantizer(struct cond_quantizer_list_t *list, uint32_t column, symbol_t prev);
void store_cond_quantizers(struct quantizer_t *restrict lo, struct quantizer_t *restrict hi, double ratio, struct cond_quantizer_list_t *list, uint32_t column, symbol_t prev);
void store_cond_quantizers_indexed(struct quantizer_t *restrict lo, struct quantizer_t *restrict hi, double ratio, struct cond_quantizer_list_t *list, uint32_t column, uint32_t index);
struct quantizer_t *choose_quantizer(struct cond_quantizer_list_t *list, uint32_t column, symbol_t prev);
uint32_t find_state_encoding(struct quantizer_t *codebook, symbol_t value);

// Meat of the implementation
void calculate_statistics(struct quality_file_t *, struct cond_pmf_list_t *);
double optimize_for_entropy(struct pmf_t *pmf, struct distortion_t *dist, double target, struct quantizer_t **lo, struct quantizer_t **hi);
struct cond_quantizer_list_t *generate_codebooks(struct quality_file_t *info, struct cond_pmf_list_t *in_pmfs, struct distortion_t *dist, double comp, double *expected_distortion);
//struct cond_quantizer_list_t *generate_codebooks_greg(struct quality_file_t *info, struct cond_pmf_list_t *in_pmfs, struct distortion_t *dist, double comp, uint32_t mode, double *expected_distortion);

// Master function to read a codebook from a file
void write_codebook(const char *filename, struct cond_quantizer_list_t *quantizers);
struct cond_quantizer_list_t *read_codebook(const char *filename, const struct alphabet_t *A);

#define MAX_CODEBOOK_LINE_LENGTH 4096
#define COPY_Q_TO_LINE(line, q, i, size) for (i = 0; i < size; ++i) { line[i] = q[i] + 33; }
#define COPY_Q_FROM_LINE(line, q, i, size) for (i = 0; i < size; ++i) { q[i] = line[i] - 33; }

#endif
