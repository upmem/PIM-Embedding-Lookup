#include "embedding.h"

#include "dpu.h"

#include <stdint.h>
#include <stdlib.h>

#define DPU_BINARY "./build/embdpu"

/** @brief global referene to dpu_set */
struct dpu_set_t dpu_set;

// static void
// copy_interval(dpu_runtime_interval *interval, struct timespec *const start,
//               struct timespec *const end) {
//     interval->start.tv_nsec = start->tv_nsec;
//     interval->start.tv_sec = start->tv_sec;
//     interval->stop.tv_nsec = end->tv_nsec;
//     interval->stop.tv_sec = end->tv_sec;
// }

/** @brief alloc dpu set with given number of dpus */
void
alloc_dpus(uint64_t nr_dpus) {
    DPU_ASSERT(dpu_alloc(nr_dpus, "nrJobsPerRank=256", &dpu_set));
    DPU_ASSERT(dpu_load(dpu_set, DPU_BINARY, NULL));
}

/** @brief transfer one embedding table params to DPU DRAM
 *  @param populate_mram(uint32_t
 *  @param embedding_id index of the embedding table to transfer
 *  @param nr_rows embedding number of rows (common to all embedding)
 *  @param table_data stores multiple embedding parameters
 */
void
populate_mram(uint64_t nr_embedding, uint64_t nr_rows, uint64_t nr_cols, int32_t **emb_tables,
              dpu_runtime_totals *runtime) {
    uint32_t nr_dpus;
    DPU_ASSERT(dpu_get_nr_dpus(dpu_set, &nr_dpus));
    assert(nr_embedding * nr_cols == (uint64_t) nr_dpus);

    /* allocates ant creates transpose embeding matrix of parameters */
    int32_t **buffer_data;
    buffer_data = (int32_t **) (malloc(nr_embedding * sizeof(int32_t *)));
    for (uint64_t embedding_index = 0; embedding_index < nr_embedding; embedding_index++) {
        buffer_data[embedding_index] = (int32_t *) (malloc(nr_rows * nr_cols * sizeof(int32_t)));

        for (uint64_t row_index = 0; row_index < nr_rows; row_index++)
            for (uint64_t col_index = 0; col_index < nr_cols; col_index++) {
                buffer_data[embedding_index][col_index * nr_rows + row_index] =
                    emb_tables[embedding_index][row_index * nr_cols + col_index];
            }
    }

    struct dpu_set_t dpu;
    uint64_t embedding_index = 0;
    uint64_t cur_emb_cols = 0;
    DPU_FOREACH(dpu_set, dpu) {
        /* set start addr of each transposed column */
        uint64_t col_start_addr = cur_emb_cols * nr_rows;
        DPU_ASSERT(dpu_prepare_xfer(dpu, &(buffer_data[embedding_index][col_start_addr])));

        cur_emb_cols++;
        if (cur_emb_cols == nr_cols) {
            embedding_index++;
            cur_emb_cols = 0;
        }
    }
    DPU_ASSERT(dpu_push_xfer(dpu_set, DPU_XFER_TO_DPU, "emb_data", 0,
                             ALIGN(nr_rows * sizeof(int32_t), 8), DPU_XFER_DEFAULT));

    /* free transposed matrix of parameters */
    for (uint64_t embedding_index = 0; embedding_index < nr_embedding; embedding_index++)
        free(buffer_data[embedding_index]);
    free(buffer_data);

    // DPU_ASSERT(dpu_launch(dpu_set, DPU_SYNCHRONOUS));
    // dpu_sync(dpu_set);
    // {
    //     struct dpu_set_t dpu;
    //     DPU_FOREACH(dpu_set, dpu) {
    //         DPU_ASSERT(dpu_log_read(dpu, stdout));
    //     }
    //     fflush(stdout);
    // }
}

/** @brief host side post processing of DPU side embedding results
 *  @param dpu_rank pointer to rank dpu set
 *  @param rank_id index of the rank
 *  @param args rank callback generic args
 */
dpu_error_t
post_process(struct dpu_set_t dpu_rank, uint64_t rank_id, void *arg) {
    struct callback_input *input = (struct callback_input *) arg;
    float **result_buffer = input->result_buffer;
    uint64_t *nr_batches = input->nr_batches;
    uint64_t nr_cols = input->nr_cols;
    uint64_t nr_embedding = input->nr_embedding;
    dpu_error_t status = DPU_OK;
    if (rank_id < nr_embedding) {
        for (int j = 0; j < nr_cols; j++) {
            for (int k = 0; k < nr_batches[rank_id]; k++)
                result_buffer[rank_id][k * nr_cols + j] =
                    (float) input->dpu_results_buffer[rank_id][j][k] * pow(10, -9);
        }
    }
    return status;
}

/** @brief perform DPU lookup operation in embedding set and for input indices of
 *        multiple batch
 *  @param indices array that stores indices [EMB_INDEX][BATCH_INDEX][INDEXES]
 *  @param offsets array that stores indices offset (pytorch EmbedingBag convention)
 *  [EMB_INDEX][BATCH_INDEX][OFFSET]
 *  @param indices_len  gives the lenght of the input indices vector for each embedding [EMB_INDEX]
 *  @param nr_batches_per_embedding gives the number of batch (same for each embedding) in indices
 *  @param result_buffer embedding lookup operation DPU results
 *  @return TBC
 */
int32_t *
lookup(uint32_t **indices, uint32_t **offsets, struct input_info *input_info, uint64_t nr_embedding,
       uint64_t nr_cols, float **result_buffer, int32_t ***dpu_result_buffer) {
    uint64_t dpu_index;
    uint64_t embedding_id;
    struct dpu_set_t dpu;
    struct query_len lengths[nr_embedding];

    // TODO: loop over embeddings
    DPU_FOREACH(dpu_set, dpu, dpu_index) {
        DPU_ASSERT(dpu_prepare_xfer(dpu, indices[(int) (dpu_index / nr_cols)]));
    }
    DPU_ASSERT(dpu_push_xfer(dpu_set, DPU_XFER_TO_DPU, "input_indices", 0,
                             ALIGN(input_info->indices_len[0] * sizeof(uint32_t), 8),
                             DPU_XFER_ASYNC));

    // TODO: loop over embeddings
    DPU_FOREACH(dpu_set, dpu, dpu_index) {
        DPU_ASSERT(dpu_prepare_xfer(dpu, offsets[(int) (dpu_index / nr_cols)]));
    }
    DPU_ASSERT(dpu_push_xfer(dpu_set, DPU_XFER_TO_DPU, "input_offsets", 0,
                             ALIGN(input_info->nr_batches_per_embedding[0] * sizeof(uint32_t), 8),
                             DPU_XFER_ASYNC));

    DPU_FOREACH(dpu_set, dpu, dpu_index) {
        embedding_id = (int) (dpu_index / nr_cols);
        // TODO : this functions support same batch size for each embedding, but
        assert(input_info->nr_batches_per_embedding[embedding_id] ==
               input_info->nr_batches_per_embedding[0]);
        lengths[embedding_id].indices_len = input_info->indices_len[0];
        lengths[embedding_id].nr_batches = input_info->nr_batches_per_embedding[embedding_id];
        DPU_ASSERT(dpu_prepare_xfer(dpu, &lengths[embedding_id]));
    }

    DPU_ASSERT(dpu_push_xfer(dpu_set, DPU_XFER_TO_DPU, "input_lengths", 0, sizeof(struct query_len),
                             DPU_XFER_ASYNC));

    DPU_ASSERT(dpu_launch(dpu_set, DPU_ASYNCHRONOUS));

    DPU_FOREACH(dpu_set, dpu, dpu_index) {
        embedding_id = dpu_index / nr_cols;
        uint64_t dpu_mod_index = dpu_index % nr_cols;
        assert(input_info->nr_batches_per_embedding[embedding_id] ==
               input_info->nr_batches_per_embedding[0]);
        DPU_ASSERT(dpu_prepare_xfer(dpu, dpu_result_buffer[embedding_id][dpu_mod_index]));
    }
    DPU_ASSERT(dpu_push_xfer(dpu_set, DPU_XFER_FROM_DPU, "results", 0,
                             ALIGN(sizeof(int32_t) * input_info->nr_batches_per_embedding[0], 8),
                             DPU_XFER_ASYNC));

    struct callback_input callback_data;
    callback_data.result_buffer = result_buffer;
    callback_data.nr_batches = input_info->nr_batches_per_embedding;
    callback_data.dpu_results_buffer = dpu_result_buffer;

    DPU_ASSERT(dpu_sync(dpu_set));

#ifdef PERFCOUNT
    uint32_t counter_init, counter_all;
    DPU_FOREACH(dpu_set, dpu, dpu_index) {
        DPU_ASSERT(
            dpu_copy_from(dpu, "counter_init", 0, &counter_init, sizeof(uint32_t)));
        DPU_ASSERT(
            dpu_copy_from(dpu, "counter_all", 0, &counter_all, sizeof(uint32_t)));
    }
    printf("DPU cycles: init %d all %d\n", counter_init, counter_all);
#endif

    // for (uint64_t embedding_index = 0; embedding_index < nr_embedding; embedding_index++) {
    //     for (uint64_t batch_index = 0; batch_index < input_info->nr_batches_per_embedding[embedding_index];
    //          batch_index++)
    //         for (uint64_t col_index = 0; col_index < nr_cols; col_index++) {
    //             result_buffer[embedding_index][batch_index * nr_cols + col_index] =
    //                 (float) callback_data.dpu_results_buffer[embedding_index][col_index][batch_index] *
    //                 pow(10, -9);
    //         }
    // }

    // read dpu logs
    // DPU_FOREACH(dpu_set, dpu, dpu_index) {
    //     // if(dpu_index==0)
    //     DPU_ASSERT(dpu_log_read(dpu, stdout));
    // }

    /* if (runtime_group && RT_CONFIG == RT_LAUNCH) {
        if(runtime_group[embedding_id].in_use >= runtime_group[embedding_id].length) {
            TIME_NOW(&end);
            fprintf(stderr,
                "ERROR: (runtime_group[%d].in_use) = %d >= runtime_group[%d].length = %d\n",
                dpu_index, runtime_group[embedding_id].in_use, embedding_id,
    runtime_group[embedding_id].length); exit(1);
        }
        copy_interval(
            &runtime_group->intervals[runtime_group[embedding_id].in_use], &start, &end);
            runtime_group[embedding_id].in_use++;
    } */
    return 0;
}
