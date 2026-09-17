#ifndef QPY_EG800ZCN_LA_PARTITION_LAYOUT_H
#define QPY_EG800ZCN_LA_PARTITION_LAYOUT_H

/*
 * Keep the application-side partition macros aligned with the gccout map.
 *
 * UniRTOS SDK 1.0.5 can be paired with an older protected partition header
 * even when a newer gccout is selected. Include the SDK header first so all
 * of its normal definitions remain available, reject unknown layouts, then
 * override only the three values changed by the QuecPython CUST layout.
*
* The SDK-integrated build also synchronizes the SDK header itself (see
* qpy_sync_sdk_partition_layout in CMakeLists.txt); this include keeps a
* per-translation-unit guarantee for the file that consumes the CUST macros.
 */
#include "unirtos_mem_partion_718pm_open.h"

#if !defined(UNIR_AP_IMG_SIZE) || !defined(UNIR_APP_OPEN_CPU_SIZE) || \
    !defined(UNIRTOS_CUST_FLASH_SIZE) || !defined(UNIR_LFS_SIZE)
#error "UniRTOS EG800ZCN_LA partition macros are unavailable"
#endif

#if UNIR_AP_IMG_SIZE != 0x1D6000
#error "Unsupported UniRTOS EG800ZCN_LA AP image layout"
#endif

#if !((UNIR_APP_OPEN_CPU_SIZE == 0xC8000 && \
       UNIRTOS_CUST_FLASH_SIZE == 0x0 && \
       UNIR_LFS_SIZE == 0x89000) || \
      (UNIR_APP_OPEN_CPU_SIZE == 0x90000 && \
       UNIRTOS_CUST_FLASH_SIZE == 0x48000 && \
       UNIR_LFS_SIZE == 0x79000))
#error "Unsupported UniRTOS EG800ZCN_LA OpenCPU/CUST/LFS layout"
#endif

#undef UNIR_APP_OPEN_CPU_SIZE
#undef UNIRTOS_CUST_FLASH_SIZE
#undef UNIR_LFS_SIZE

#define UNIR_APP_OPEN_CPU_SIZE  (0x90000)
#define UNIRTOS_CUST_FLASH_SIZE (0x48000)
#define UNIR_LFS_SIZE            (0x79000)

#endif
