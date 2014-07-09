#include <stdlib.h>
#include <stdio.h>
#include <malloc.h>
#include <float.h>

#include "quantizer.h"
#include "util.h"

/**
 * Allocate enough room based on the size of the alphabet supplied
 */
struct quantizer_t *alloc_quantizer(const struct alphabet_t *alphabet) {
	struct quantizer_t *rtn = (struct quantizer_t *) calloc(1, sizeof(struct quantizer_t));
	rtn->alphabet = alphabet;
	rtn->q = (symbol_t *) calloc(alphabet->size, sizeof(symbol_t));
	return rtn;
}

/**
 * Free the quantizer itself but not the alphabet
 */
void free_quantizer(struct quantizer_t *q) {
	free(q->q);
	free(q);
}

/**
 * Produce a quantizer with the given number of states for the given pmf, and
 * optionally computes the expected distortion produced by this quantizer.
 * The bounds array here contains the left endpoint (inclusive) of each region
 */
struct quantizer_t *generate_quantizer(struct pmf_t *pmf, struct distortion_t *dist, uint32_t states, double *dist_out) {
	struct quantizer_t *q = alloc_quantizer(pmf->alphabet);
	uint32_t changed = 1;
	uint32_t iter = 0;
	uint32_t i, j, r, size;
	uint32_t min_r;
	double mse, min_mse, next_mse;
	symbol_t *bounds = (symbol_t *) _alloca((states+1)*sizeof(symbol_t));
	symbol_t *reconstruction = (symbol_t *) _alloca(states*sizeof(symbol_t));

	// Initial bounds and reconstruction points
	bounds[0] = 0;
	bounds[states] = pmf->alphabet->size;
	for (j = 1; j < states; ++j) {
		bounds[j] = (j * pmf->alphabet->size) / states;
	}
	for (j = 0; j < states; ++j) {
		reconstruction[j] = (bounds[j] + bounds[j+1] - 1) / 2;
	}

	// Lloyd-Max quantizer design alternating between adjustment of bounds
	// and of reconstruction point locations until there is no change
	size = pmf->alphabet->size;
	while (changed && iter < QUANTIZER_MAX_ITER) {
		changed = 0;
		iter += 1;

		// First, adjust the reconstruction points for fixed bounds
		for (j = 0; j < states; ++j) {
			// Initial guess for min values
			min_mse = DBL_MAX;
			min_r = bounds[j];
			
			// For each possible reconstruction point
			for (r = bounds[j]; r < bounds[j+1]; ++r) {
				// Find its distortion when used for the whole region
				mse = 0.0;
				for (i = bounds[j]; i < bounds[j+1]; ++i) {
					mse += get_probability(pmf, i) * get_distortion(dist, i, r);
				}

				// Compare to minimums, save if better
				if (mse < min_mse) {
					min_r = r;
					min_mse = mse;
				}
			}

			// Check if we've changed our reconstruction and save it
			if (min_r != reconstruction[j]) {
				changed = 1;
				reconstruction[j] = min_r;
			}
		}

		// Then, adjust the bounds for fixed reconstruction points by iterating
		// over the positions (apart from the endpoints which always have a fixed
		// assignment) and deciding which of the two nearest points they
		// contribute the least expected distortion to
		r = 0;
		for (j = 1; j < size-1 && r < states-1; ++j) {
			// Get distortion for the current and next reconstruction points
			// I don't think the PMF actually affects this since it is the same
			// coefficient for both and we are comparing them
			mse = get_distortion(dist, j, reconstruction[r]);
			next_mse = get_distortion(dist, j, reconstruction[r+1]);

			// if the next one is lower, save the current symbol as the left bound
			// for that region
			if (next_mse < mse) {
				r += 1;
				bounds[r] = j;
			}
		}
	}

	// Now, iterate over the regions and set up the quantizer mapping from input
	// to reconstruction point
	for (j = 0; j < states; ++j) {
		for (i = bounds[j]; i < bounds[j+1]; ++i) {
			q->q[i] = reconstruction[j];
		}
	}

	// Save the output alphabet in the quantizer
	// TODO:

	// If requested, calculate the expected distortion for the final assignment
	// and return it to the caller
	if (dist_out != NULL) {
		mse = 0.0;
		for (j = 0; j < states; ++j) {
			for (i = bounds[j]; i < bounds[j+1]; ++i) {
				mse += get_distortion(dist, i, reconstruction[j]) * get_probability(pmf, i);
			}
		}
		*dist_out = mse;
	}

	return q;
}

/**
 * Calculate the PMF of the output when the given quantizer is used with symbols generated
 * from the given input distribution. The input and output pmf structures cannot be the
 * same
 */
void apply_quantizer(struct quantizer_t *q, struct pmf_t *restrict pmf, struct pmf_t *restrict output) {
	uint32_t i;

	if (!pmf->pmf_ready)
		recalculate_pmf(pmf);

	// Clear existing pmf from output
	memset(output->pmf, 0, output->alphabet->size * sizeof(double));

	// Sum together input probabilities that map to the same output
	for (i = 0; i < pmf->alphabet->size; ++i) {
		output[q->q[i]] += get_probability(pmf, i);
	}
	output->pmf_ready = 1;
}

/**
 * Print a quantizer to stdout
 */
void print_quantizer(struct quantizer_t *q) {
	uint32_t i;
	char *tmp = (char *) _alloca(q->alphabet->size+1);
	tmp[q->alphabet->size] = 0;

	for (i = 0; i < q->alphabet->size; ++i) {
		tmp[i] = (char) (q->q[i] + 33);
	}

	printf("Quantizer: %s\n", tmp);
	// TODO: Print unique output alphabet
}