/**
 * @file device_profile.c
 * @brief 产品档案表与查询
 */

#include "device_profile.h"

static const device_product_profile_t s_product_profiles[] = {
    {
        .product_id = DEVICE_PRODUCT_ID_FACTORY,
        .name = "factory",
        .board_mask = DEVICE_BOARD_MASK_FULL,
        .platform_mask = DEVICE_PLATFORM_MASK_LOG | DEVICE_PLATFORM_MASK_CMD | DEVICE_PLATFORM_MASK_BUTTON,
        .clock_source = DEVICE_CLOCK_MAIN_8MHZ,
    },
    {
        .product_id = DEVICE_PRODUCT_ID_CAR_4WD_FULL,
        .name = "car-4wd",
        .board_mask = DEVICE_BOARD_MASK_FULL,
        .platform_mask = DEVICE_PLATFORM_MASK_LOG | DEVICE_PLATFORM_MASK_BUTTON,
        .clock_source = DEVICE_CLOCK_MAIN_8MHZ,
    },
    {
        .product_id = DEVICE_PRODUCT_ID_CAR_2WD_FULL,
        .name = "car-2wd",
        .board_mask = DEVICE_BOARD_MASK_FULL,
        .platform_mask = DEVICE_PLATFORM_MASK_LOG | DEVICE_PLATFORM_MASK_BUTTON,
        .clock_source = DEVICE_CLOCK_MAIN_8MHZ,
    },
};

static const device_product_profile_t *product_profile_lookup(uint32_t product_id)
{
    size_t i;

    for (i = 0U; i < (sizeof(s_product_profiles) / sizeof(s_product_profiles[0])); i++) {
        if (s_product_profiles[i].product_id == product_id) {
            return &s_product_profiles[i];
        }
    }

    /* Unknown id → car-4wd (index 1), not factory. */
    return &s_product_profiles[1];
}

const device_product_profile_t *device_profile_product(void)
{
    return product_profile_lookup((uint32_t)DEVICE_PRODUCT_ID);
}

bool device_profile_board_wants(uint32_t mask)
{
    const device_product_profile_t *profile = device_profile_product();

    if (profile == NULL) {
        return false;
    }

    return (profile->board_mask & mask) != 0U;
}

bool device_profile_platform_wants(uint32_t mask)
{
    const device_product_profile_t *profile = device_profile_product();

    if (profile == NULL) {
        return false;
    }

    return (profile->platform_mask & mask) != 0U;
}
