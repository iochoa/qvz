#include <assert.h>
#include "qv_compressor.h"

/**
 * Compress a quality value and send it into the arithmetic encoder output stream,
 * with appropriate context information
 */
void compress_qv(arithStream as, uint32_t x, uint8_t cluster, uint32_t column, uint32_t idx) {
    arithmetic_encoder_step(as->a, as->stats[cluster][column][idx], x, as->os);
    update_stats(as->stats[cluster][column][idx], x, as->a->r);
}

/**
 * Writes a cluster value to the arithmetic encoder
 * We don't need to do adaptive stats here but it saves us a number of bytes
 * on writing the number of lines in each cluster.
 * @todo Determine which has a lower bitrate (probably almost the same)
 */
void qv_write_cluster(arithStream as, uint8_t cluster) {
	arithmetic_encoder_step(as->a, as->cluster_stats, cluster, as->os);
	update_stats(as->cluster_stats, cluster, as->a->r);
}

/**
 * Retrieve a quality value from the arithmetic decoder input stream
 */
uint32_t decompress_qv(arithStream as, uint8_t cluster, uint32_t column, uint32_t idx) {
    uint32_t x;
    
    x = arithmetic_decoder_step(as->a, as->stats[cluster][column][idx], as->os);
    update_stats(as->stats[cluster][column][idx], x, as->a->r);
    
    return x;
}

uint8_t qv_read_cluster(arithStream as) {
	uint32_t x;
	
	x = arithmetic_decoder_step(as->a, as->cluster_stats, as->os);
	update_stats(as->cluster_stats, x, as->a->r);

	return (uint8_t) x;
}

/**
 * Compress a sequence of quality scores including dealing with organization by cluster
 */
uint32_t start_qv_compression(struct quality_file_t *info, FILE *fout, double *dis) {
    unsigned int osSize = 0;
    
    qv_compressor qvc;
    
	uint32_t s = 0, idx = 0, q_state = 0;
	double distortion = 0.0;
	uint32_t error = 0;
    uint8_t qv = 0, prev_qv = 0;
    uint32_t columns = info->columns;
    struct quantizer_t *q;
	struct cond_quantizer_list_t *qlist;

	uint32_t block_idx, line_idx;
	uint8_t cluster_id;

	struct line_t *line;
    
#ifdef DEBUG
    FILE *fref = fopen("fref.txt", "wt");
#endif
    
    // Initialize the compressor
    qvc = initialize_qv_compressor(fout, COMPRESSION, info);
    
    // Start compressing the file
	distortion = 0.0;
	block_idx = 0;
	line_idx = 0;

	do {
		line = &info->blocks[block_idx].lines[line_idx];
		
        if (info->opts->verbose && line_idx == 0) {
            printf("Line: %dM\n", block_idx);
        }

		// Write clustering information and pull the correct codebook
		cluster_id = line->cluster;
		qlist = info->clusters->clusters[cluster_id].qlist;
		qv_write_cluster(qvc->Quals, cluster_id);
        
		// Select first column's codebook with no left context
		q = choose_quantizer(qlist, &info->well, 0, 0, &idx);
        
		// Quantize, compress and calculate error simultaneously
		// Reading in the lines corrects the quality values to alphabet offsets
		qv = q->q[line->data[0]];
        q_state = get_symbol_index(q->output_alphabet, qv);
        compress_qv(qvc->Quals, q_state, cluster_id, 0, idx);
        
#ifdef DEBUG
		fputc(qv+33, fref);
#endif
        
		// @todo use distortion function
		error = (line->data[0] - qv)*(line->data[0] - qv);
        prev_qv = qv;
        
		for (s = 1; s < columns; ++s) {
			q = choose_quantizer(qlist, &info->well, s, prev_qv, &idx);
			qv = q->q[line->data[s]];
            q_state = get_symbol_index(q->output_alphabet, qv);
            
#ifdef DEBUG
			fputc(qv+33, fref);
#endif
            
            compress_qv(qvc->Quals, q_state, cluster_id, s, idx);
			error += (line->data[s] - qv)*(line->data[s] - qv);
            prev_qv = qv;
		}
        
#ifdef DEBUG
       	fputc('\n', fref);
#endif
        
        distortion += error / ((double) columns);

		// Set up next set of pointers
		line_idx += 1;
		if (line_idx == info->blocks[block_idx].count) {
			line_idx = 0;
			block_idx += 1;
		}
	} while (block_idx < info->block_count);
    
    osSize = encoder_last_step(qvc->Quals->a, qvc->Quals->os);
    
	if (dis)
    	*dis = distortion / ((double) info->lines);
    
    return osSize;
}

void start_qv_decompression(FILE *fout, FILE *fin, struct quality_file_t *info) {
    qv_compressor qvc;
    
	uint32_t s = 0, idx = 0, lineCtr = 0, q_state = 0;
    uint8_t prev_qv = 0, cluster_id;
    
    uint32_t columns = info->columns;
	uint32_t lines = info->lines;
	struct cond_quantizer_list_t *qlist;
    struct quantizer_t *q;

	char *line = (char *) _alloca(columns+2);
    line[columns] = '\n';
	line[columns+1] = '\0';
    
    // Initialize the compressor
    qvc = initialize_qv_compressor(fin, DECOMPRESSION, info);
    
	// Last line has to be handled separately to clear the arithmetic decoder
	while (lineCtr < lines - 1) {
        if (info->opts->verbose && lineCtr%1000000 == 0){
            printf("Line: %dM\n", lineCtr/1000000);
        }
        lineCtr++;

		cluster_id = qv_read_cluster(qvc->Quals);
		assert(cluster_id < info->cluster_count);
		qlist = info->clusters->clusters[cluster_id].qlist;
        
		// Select first column's codebook with no left context
		q = choose_quantizer(qlist, &info->well, 0, 0, &idx);
        
		// Quantize, compress and calculate error simultaneously
		// Note that in this version the quantizer outputs are 0-41, so the +33 offset is different from before
        q_state = decompress_qv(qvc->Quals, cluster_id, 0, idx);
        line[0] = q->output_alphabet->symbols[q_state] + 33;
        prev_qv = line[0] - 33;
        
		for (s = 1; s < columns; ++s) {
			// Quantize and compute error for MSE
			q = choose_quantizer(qlist, &info->well, s, prev_qv, &idx);
            q_state = decompress_qv(qvc->Quals, cluster_id, s, idx);
            line[s] = q->output_alphabet->symbols[q_state] + 33;
            prev_qv = line[s] - 33;
		}
        
        // Write this line to the output file, note '\n' at the end of the line buffer to get the right length
		fwrite(line, columns+1, sizeof(uint8_t), fout);
	}
    
    // Last Line
    if (info->opts->verbose && lineCtr%1000000 == 0){
        printf("Line: %dM\n", lineCtr/1000000);
    }
    lineCtr++;
    
	cluster_id = qv_read_cluster(qvc->Quals);
		assert(cluster_id < info->cluster_count);
	qlist = info->clusters->clusters[cluster_id].qlist;

    // Select first column's codebook with no left context
    q = choose_quantizer(qlist, &info->well, 0, 0, &idx);
    
    // Quantize, compress and calculate error simultaneously
    // Note that in this version the quantizer outputs are 0-41, so the +33 offset is different from before
    q_state = decompress_qv(qvc->Quals, cluster_id, 0, idx);
    line[0] = q->output_alphabet->symbols[q_state] + 33;
    prev_qv = line[0] - 33;
    
    for (s = 1; s < columns - 1; ++s) {
        // Quantize and compute error for MSE
        q = choose_quantizer(qlist, &info->well, s, prev_qv, &idx);
        q_state = decompress_qv(qvc->Quals, cluster_id, s, idx);
        line[s] = q->output_alphabet->symbols[q_state] + 33;
        prev_qv = line[s] - 33;
    }
    
    // Last column
    q = choose_quantizer(qlist, &info->well, s, prev_qv, &idx);
    q_state = decoder_last_step(qvc->Quals->a, qvc->Quals->stats[cluster_id][s][idx]);
    line[s] = q->output_alphabet->symbols[q_state] + 33;
    
    // Write this line to the output file, note '\n' at the end of the line buffer to get the right length
    fwrite(line, columns+1, sizeof(uint8_t), fout);

	info->lines = lineCtr;
}
