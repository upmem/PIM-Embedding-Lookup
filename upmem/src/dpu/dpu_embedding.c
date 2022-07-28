// To build the code: dpu-upmem-dpurte-clang -o toy_dpu toy_dpu.c
#include "common.h"
#include "common/include/common.h"
#include "emb_types.h"

#include <attributes.h>
#include <barrier.h>
#include <defs.h>
#include <mram.h>
#include <perfcounter.h>
#include <stdint.h>

__dma_aligned int32_t tmp_emb_data[NR_TASKLETS][MAX_NR_COLS_PER_DPU];
__host uint64_t nr_cols;
__mram_noinit int32_t emb_data[DPU_EMB_DATA_SIZE];
// #define PERFCOUNT
#ifdef PERFCOUNT
__host uint32_t counter_all, counter_init;
#endif

BARRIER_INIT(my_barrier, NR_TASKLETS);

int
main() {

    if (me() == 0) {

        printf("nr cols %lu\n", nr_cols);
        for (uint64_t r_index = 0; r_index < 4; r_index++) {
            mram_read(&emb_data[r_index * nr_cols], tmp_emb_data[0],
                      ALIGN(nr_cols * sizeof(int32_t), 8));
            for (uint64_t col_index = 0; col_index < nr_cols; col_index++) {
                printf("d %d, ", tmp_emb_data[0][col_index]);
            }
            printf("emb %p", emb_data);
            printf("\n");
        }
    }
    return 0;
}
