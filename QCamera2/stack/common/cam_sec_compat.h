/*
 * Compatibility glue between this CAF QCamera2 HAL and the Samsung a9y18qlte
 * cam_intf.h/cam_types.h layout (reconstructed from camera.sdm660.so).
 * The stack/common headers mirror Samsung exactly; only HAL-side shims go here.
 */
#ifndef __CAM_SEC_COMPAT_H__
#define __CAM_SEC_COMPAT_H__

/* Samsung has no IS_TYPE_VENDOR_EIS (IS_TYPE_MAX = 6): never matches. */
#define IS_TYPE_VENDOR_EIS ((cam_is_type_t)0xff)

/* Samsung's backend splits large frames across both VFEs itself and its
 * capability has no single_isp_max_size; use the full active array so no
 * supported size is treated as needing PP upscaling or ISP capping. */
#define SEC_SINGLE_ISP_MAX_SIZE(cap) \
    ((cam_dimension_t){ (cap)->active_array_size.width, (cap)->active_array_size.height })

#endif /* __CAM_SEC_COMPAT_H__ */
