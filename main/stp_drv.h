#ifndef __STP_DRV__H__
#define __STP_DRV__H__

#include <stdint.h>
#include "driver/gpio.h"
#include "driver/gptimer.h"

#define S_CURVE_ARR_LEN     (300/5)

typedef struct
{
    // io
    gpio_num_t enable;
    gpio_num_t tx;
    gpio_num_t rx;
    gpio_num_t dir;
    gpio_num_t step;
    gpio_num_t spread;

    // "private" variables
    uint8_t dir_set;
    uint16_t rpm_set;
    uint16_t duty_set;
    uint8_t dir_invert;

    //
    uint16_t s_cruve_arr[S_CURVE_ARR_LEN];
    uint16_t s_curve_steps;
    uint16_t s_cruve_index;
    uint32_t tick;

    // counters
    int32_t step_position;
    int32_t step_target;

    //
    gptimer_handle_t gptimer;
    gptimer_alarm_config_t alarm_config;
} tmc2209_io_t;

void stepper_init(tmc2209_io_t *stp);

void stepper_set_invert(tmc2209_io_t *stp, uint8_t inverted);

void stepper_set_position(tmc2209_io_t *stp, int32_t position);

void stepper_go_to_pos(tmc2209_io_t *stp, uint32_t rpm_set, int32_t position);

void stepper_stop(tmc2209_io_t *stp);

uint8_t stepper_ready(tmc2209_io_t *stp);

int stepper_read_reg(tmc2209_io_t *stp, uint8_t reg, uint32_t *data);

int stepper_write_reg(tmc2209_io_t *stp, uint8_t reg, uint32_t data);

#endif