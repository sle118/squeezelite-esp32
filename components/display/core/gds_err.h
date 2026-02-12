#ifndef _GDS_ERR_H_
#define _GDS_ERR_H_

#include <esp_log.h>

#define GDS_DoAbort()

#if !defined NullCheck
#define NullCheck(ptr, retexpr)                                                                                                                      \
    {                                                                                                                                                \
        if(ptr == NULL) {                                                                                                                            \
            ESP_LOGE(__FUNCTION__, "%s == NULL", #ptr);                                                                                              \
            GDS_DoAbort();                                                                                                                           \
            retexpr;                                                                                                                                 \
        }                                                                                                                                            \
    }
#endif

#if !defined ESP_ERROR_CHECK_NONFATAL
#define ESP_ERROR_CHECK_NONFATAL(expr, retexpr)                                                                                                      \
    {                                                                                                                                                \
        esp_err_t __err_rc = (expr);                                                                                                                 \
        if(__err_rc != ESP_OK) {                                                                                                                     \
            ESP_LOGE(__FUNCTION__, "%s != ESP_OK, result: %d", #expr, __err_rc);                                                                     \
            GDS_DoAbort();                                                                                                                           \
            retexpr;                                                                                                                                 \
        }                                                                                                                                            \
    }
#endif

#if !defined CheckBounds
#define CheckBounds(expr, retexpr)                                                                                                                   \
    {                                                                                                                                                \
        if(expr) {                                                                                                                                   \
            ESP_LOGE(__FUNCTION__, "Line %d: %s", __LINE__, #expr);                                                                                  \
            GDS_DoAbort();                                                                                                                           \
            retexpr;                                                                                                                                 \
        }                                                                                                                                            \
    }
#endif

#endif
