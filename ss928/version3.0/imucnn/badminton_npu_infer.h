#ifndef BADMINTON_NPU_INFER_H
#define BADMINTON_NPU_INFER_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BADMINTON_NPU_NUM_CHANNELS 8
#define BADMINTON_NPU_WINDOW_SIZE  24
#define BADMINTON_NPU_NUM_CLASSES  6

typedef struct {
    int class_id;
    float confidence;
    float logits[BADMINTON_NPU_NUM_CLASSES];
} badminton_npu_result_t;

int badminton_npu_init(const char *om_path);
void badminton_npu_deinit(void);
int badminton_npu_is_ready(void);

/* raw window [8][24], same units as training (mg / dps / deg / g) */
int badminton_npu_infer(const float input[BADMINTON_NPU_NUM_CHANNELS][BADMINTON_NPU_WINDOW_SIZE],
    badminton_npu_result_t *result);

const char *badminton_npu_label_cn(int class_id);

#ifdef __cplusplus
}
#endif

#endif
