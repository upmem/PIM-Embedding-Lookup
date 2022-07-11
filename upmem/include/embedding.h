#include "common.h"
#include "emb_types.h"
#include "host/include/host.h"

#include <assert.h>
#include <dpu.h>
#include <dpu_log.h>
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#define TIME_NOW(_t) (clock_gettime(CLOCK_MONOTONIC, (_t)))

/**
 * @brief TBC
 * @param TBC
 */
typedef struct dpu_runtime_totals {
    double execution_time_prepare;
    double execution_time_populate_copy_in;
    double execution_time_copy_in;
    double execution_time_copy_out;
    double execution_time_aggregate_result;
    double execution_time_launch;
} dpu_runtime_totals;

/**
 * @brief TBC
 * @param TBC
 */
typedef struct dpu_timespec {
    long tv_nsec;
    long tv_sec;
} dpu_timespec;

/**
 * @brief TBC
 * @param TBC
 */
typedef struct dpu_runtime_interval {
    dpu_timespec start;
    dpu_timespec stop;
} dpu_runtime_interval;

/**
 * @brief TBC
 * @param TBC
 */
typedef struct dpu_runtime_group {
    unsigned int in_use;
    unsigned int length;
    dpu_runtime_interval *intervals;
} dpu_runtime_group;

struct input_info {
    uint64_t *nr_batches_per_embedding;
    uint64_t *indices_len;
};
typedef struct input_batch {
    bool valid;
    uint32_t **indices;
    uint32_t **offsets;
    struct input_info *input_info;
} input_batch;

typedef struct embeding_rank_mapping {
    uint32_t nr_ranks;
    uint32_t *rank_nr_dpus;
    uint32_t *rank_nr_embedding;
    uint32_t **embedding_index;
    uint32_t **embedding_chunk_nr_col;
    uint32_t **embedding_chunk_start_col;
} embedding_rank_mapping;

// static void
// copy_interval(dpu_runtime_interval *interval, struct timespec *const start,
//               struct timespec *const end);

void
alloc_dpus(uint64_t nr_dpus);

void
free_embedding_rank_mapping(embedding_rank_mapping *rank_mapping);

embedding_rank_mapping *
populate_mram(uint64_t nr_embedding, uint64_t nr_rows, uint64_t nr_cols, int32_t **table_data,
              dpu_runtime_totals *runtime);

dpu_error_t
post_process(struct dpu_set_t dpu_rank, uint64_t rank_id, void *arg);

int32_t *
lookup(uint32_t **indices, uint32_t **offsets, struct input_info *input_info,
       embedding_rank_mapping *rank_mapping_info, uint64_t nr_embedding, uint64_t nr_cols,
       float **result_buffer, int32_t ***dpu_result_buffer);

void
free_embedding_dpu_backend();

void
alloc_embedding_dpu_backend();
