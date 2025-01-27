#pragma once
#include "api_common.h"
#include "backend_custom_param.h"

#ifdef __cplusplus
extern "C" {
#endif

void api_swapchannel_global(
    global_tensor_spec_t *input,
    global_tensor_spec_t *output,
    Data *param);

#ifdef __cplusplus
}
#endif
