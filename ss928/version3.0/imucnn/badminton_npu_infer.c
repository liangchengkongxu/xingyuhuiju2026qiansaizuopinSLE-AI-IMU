#include "badminton_npu_infer.h"

#include "badminton_preprocess.h"

#include "acl.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static uint32_t g_model_id = 0;
static aclmdlDesc *g_model_desc = NULL;
static aclmdlDataset *g_input_dataset = NULL;
static aclmdlDataset *g_output_dataset = NULL;
static void *g_input_dev = NULL;
static void *g_output_dev = NULL;
static size_t g_in_size = 0;
static size_t g_out_size = 0;
static int g_ready = 0;

static const char *g_label_cn[BADMINTON_NPU_NUM_CLASSES] = {
    "高远", "平抽", "挑球", "放网", "发球", "杀球"
};

const char *badminton_npu_label_cn(int class_id)
{
    if (class_id < 0 || class_id >= BADMINTON_NPU_NUM_CLASSES) {
        return "未知";
    }
    return g_label_cn[class_id];
}

static void badminton_normalize(const float in[BADMINTON_NPU_NUM_CHANNELS][BADMINTON_NPU_WINDOW_SIZE],
    float out[BADMINTON_NPU_NUM_CHANNELS * BADMINTON_NPU_WINDOW_SIZE])
{
    int c;
    int t;
    for (c = 0; c < BADMINTON_NPU_NUM_CHANNELS; ++c) {
        float inv_std = 1.0f / g_badminton_std[c];
        for (t = 0; t < BADMINTON_NPU_WINDOW_SIZE; ++t) {
            out[c * BADMINTON_NPU_WINDOW_SIZE + t] = (in[c][t] - g_badminton_mean[c]) * inv_std;
        }
    }
}

static void badminton_softmax(const float *logits, int n, float *probs)
{
    float max_v = logits[0];
    float sum = 0.0f;
    int i;
    for (i = 1; i < n; ++i) {
        if (logits[i] > max_v) {
            max_v = logits[i];
        }
    }
    for (i = 0; i < n; ++i) {
        probs[i] = expf(logits[i] - max_v);
        sum += probs[i];
    }
    if (sum > 0.0f) {
        for (i = 0; i < n; ++i) {
            probs[i] /= sum;
        }
    }
}

int badminton_npu_is_ready(void)
{
    return g_ready;
}

int badminton_npu_init(const char *om_path)
{
    aclError ret;

    if (g_ready != 0) {
        return 0;
    }
    if (om_path == NULL || om_path[0] == '\0') {
        return -1;
    }

    ret = aclInit("");
    if (ret != ACL_SUCCESS && ret != ACL_ERROR_REPEAT_INITIALIZE) {
        printf("badminton_npu: aclInit failed ret=%d\n", (int)ret);
        return -1;
    }
    ret = aclrtSetDevice(0);
    if (ret != ACL_SUCCESS) {
        printf("badminton_npu: aclrtSetDevice failed ret=%d\n", (int)ret);
        return -1;
    }

    ret = aclmdlLoadFromFile(om_path, &g_model_id);
    if (ret != ACL_SUCCESS) {
        printf("badminton_npu: load failed ret=%d path=%s\n", (int)ret, om_path);
        return -1;
    }

    g_model_desc = aclmdlCreateDesc();
    if (g_model_desc == NULL) {
        return -1;
    }
    ret = aclmdlGetDesc(g_model_desc, g_model_id);
    if (ret != ACL_SUCCESS) {
        printf("badminton_npu: get desc failed ret=%d\n", (int)ret);
        badminton_npu_deinit();
        return -1;
    }

    g_in_size = aclmdlGetInputSizeByIndex(g_model_desc, 0);
    g_out_size = aclmdlGetOutputSizeByIndex(g_model_desc, 0);
    ret = aclrtMalloc(&g_input_dev, g_in_size, ACL_MEM_MALLOC_HUGE_FIRST);
    if (ret != ACL_SUCCESS) {
        badminton_npu_deinit();
        return -1;
    }
    ret = aclrtMalloc(&g_output_dev, g_out_size, ACL_MEM_MALLOC_HUGE_FIRST);
    if (ret != ACL_SUCCESS) {
        badminton_npu_deinit();
        return -1;
    }

    g_input_dataset = aclmdlCreateDataset();
    g_output_dataset = aclmdlCreateDataset();
    if (g_input_dataset == NULL || g_output_dataset == NULL) {
        badminton_npu_deinit();
        return -1;
    }

    {
        aclDataBuffer *in_buf = aclCreateDataBuffer(g_input_dev, g_in_size);
        aclDataBuffer *out_buf = aclCreateDataBuffer(g_output_dev, g_out_size);
        if (in_buf == NULL || out_buf == NULL) {
            if (in_buf != NULL) {
                aclDestroyDataBuffer(in_buf);
            }
            if (out_buf != NULL) {
                aclDestroyDataBuffer(out_buf);
            }
            badminton_npu_deinit();
            return -1;
        }
        (void)aclmdlAddDatasetBuffer(g_input_dataset, in_buf);
        (void)aclmdlAddDatasetBuffer(g_output_dataset, out_buf);
    }

    g_ready = 1;
    printf("badminton_npu: ready om=%s in=%zu out=%zu\n", om_path, g_in_size, g_out_size);
    return 0;
}

void badminton_npu_deinit(void)
{
    if (g_input_dataset != NULL) {
        aclmdlDestroyDataset(g_input_dataset);
        g_input_dataset = NULL;
    }
    if (g_output_dataset != NULL) {
        aclmdlDestroyDataset(g_output_dataset);
        g_output_dataset = NULL;
    }
    if (g_input_dev != NULL) {
        (void)aclrtFree(g_input_dev);
        g_input_dev = NULL;
    }
    if (g_output_dev != NULL) {
        (void)aclrtFree(g_output_dev);
        g_output_dev = NULL;
    }
    if (g_model_desc != NULL) {
        aclmdlDestroyDesc(g_model_desc);
        g_model_desc = NULL;
    }
    if (g_model_id != 0) {
        (void)aclmdlUnload(g_model_id);
        g_model_id = 0;
    }
    g_ready = 0;
}

int badminton_npu_infer(const float input[BADMINTON_NPU_NUM_CHANNELS][BADMINTON_NPU_WINDOW_SIZE],
    badminton_npu_result_t *result)
{
    float host_in[BADMINTON_NPU_NUM_CHANNELS * BADMINTON_NPU_WINDOW_SIZE];
    float host_out[BADMINTON_NPU_NUM_CLASSES];
    float probs[BADMINTON_NPU_NUM_CLASSES];
    aclError ret;
    int best = 0;
    int i;

    if (g_ready == 0 || result == NULL || input == NULL) {
        return -1;
    }

    badminton_normalize(input, host_in);
    ret = aclrtMemcpy(g_input_dev, g_in_size, host_in, sizeof(host_in), ACL_MEMCPY_HOST_TO_DEVICE);
    if (ret != ACL_SUCCESS) {
        return -1;
    }

    ret = aclmdlExecute(g_model_id, g_input_dataset, g_output_dataset);
    if (ret != ACL_SUCCESS) {
        printf("badminton_npu: execute failed ret=%d\n", (int)ret);
        return -1;
    }

    ret = aclrtMemcpy(host_out, sizeof(host_out), g_output_dev,
        g_out_size < sizeof(host_out) ? g_out_size : sizeof(host_out), ACL_MEMCPY_DEVICE_TO_HOST);
    if (ret != ACL_SUCCESS) {
        return -1;
    }

    for (i = 0; i < BADMINTON_NPU_NUM_CLASSES; ++i) {
        result->logits[i] = host_out[i];
    }
    badminton_softmax(result->logits, BADMINTON_NPU_NUM_CLASSES, probs);

    best = 0;
    for (i = 1; i < BADMINTON_NPU_NUM_CLASSES; ++i) {
        if (probs[i] > probs[best]) {
            best = i;
        }
    }
    result->class_id = best;
    result->confidence = probs[best];
    return 0;
}
