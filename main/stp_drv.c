#include "stp_drv.h"
#include "driver/uart.h"
#include "driver/gptimer.h"
#include "esp_log.h"
#include <math.h>

#define RPM_TO_PERIOD(rpm) ((CLOCK_PWM) / ((rpm / 60) * STP_STEP_PER_RPM))

#define S_CURVE_FLEX    3

static bool IRAM_ATTR example_timer_on_alarm_cb_v1(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *user_data)
{
    tmc2209_io_t *stp = (tmc2209_io_t *) user_data;

    if (stp->step_position != stp->step_target) {
        gpio_set_level(stp->step, 1);
        gpio_set_level(stp->step, 0);
        
        if (stp->dir_set == 0) {
            stp->step_position++;
        }
        else {
            stp->step_position--;
        }
    }

    return false;
}

static void CalculateSModelLine(uint16_t period[], float len, float fre_max, float fre_min, float flexible)
{
    float deno;
    float melo;
    float delt = fre_max - fre_min;
    float fre = 0;
    for (int i = 0; i < len; i++)
    {
        melo = flexible * (i - len / 2) / (len / 2);
        deno = 1.0 / (1 + expf(-melo)); // expf is a library function of exponential(e)
        fre = delt * deno + fre_min;
        fre /= 60;
        fre *= STP_STEP_PER_RPM;
        period[i] = (uint16_t)fre;
    }
}

static void stepper_handle(tmc2209_io_t *stp)
{
    // no movement?
    if (stp->s_curve_steps == 0)
        return;

    // time to do a step?
    if (!(stp->tick - xTaskGetTickCount()))
        return;
    
    // update timer
    stp->tick = xTaskGetTickCount();
    
    // reached our end position?
    if (stp->step_position == stp->step_target) {
        // still active?
        if (stp->duty_set) {
            gptimer_stop(stp->gptimer);
            stp->duty_set = 0;
            stp->s_curve_steps = 0;
            stp->s_cruve_index = 0;

            ESP_LOGI("SYS", "Target position reached");
        }
    }
    // we are moving
    else {
        // what is our position difference?
        int step_target_offset = abs(stp->step_target - stp->step_position);

        ESP_LOGI("STP", "%d %d %d", (int)stp->step_target, (int)stp->step_position, step_target_offset);

        // nearing the end of our movement?
        if (step_target_offset < (stp->rpm_set * 25)) {
            // first time since start? Happens for small steps
            if (stp->duty_set == 0) {
                stp->duty_set = 800;
                stp->alarm_config.alarm_count = 1000000 / stp->duty_set;
                stp->alarm_config.flags.auto_reload_on_alarm = true;
                gptimer_set_alarm_action(stp->gptimer, &stp->alarm_config);
                gptimer_start(stp->gptimer);
                ESP_LOGI("SYS", "Duty set to %d", (int)stp->duty_set);
            }

            // we are in an s-curve?
            if (stp->s_cruve_index) {
                // do the next step
                stp->duty_set = stp->s_cruve_arr[stp->s_cruve_index] - 1;
                stp->alarm_config.alarm_count = 1000000 / stp->duty_set;
                stp->alarm_config.flags.auto_reload_on_alarm = true;
                gptimer_set_alarm_action(stp->gptimer, &stp->alarm_config);
                ESP_LOGI("SYS", "Duty set to %d", (int)stp->duty_set);
                
                // decrement
                stp->s_cruve_index--;
            }
        }
        // Starting movement?
        else if (stp->s_cruve_index != stp->s_curve_steps)
        {
            // first time since start?
            if (stp->duty_set == 0) {
                // set dir
                if (!stp->dir_invert)
                    gpio_set_level(stp->dir, stp->dir_set);
                else
                    gpio_set_level(stp->dir, !stp->dir_set);

                // direct start?
                if (stp->s_curve_steps == 1) {
                    stp->duty_set = stp->rpm_set * STP_STEP_PER_RPM / 60;
                }
                // not direct start
                else {
                    // calculate s cruve
                    CalculateSModelLine(stp->s_cruve_arr, stp->s_curve_steps + 1, stp->rpm_set, 30, S_CURVE_FLEX);

                    // set duty cycle
                    stp->duty_set = stp->s_cruve_arr[stp->s_cruve_index] - 1;
                }
                
                // start timer
                stp->alarm_config.alarm_count = 1000000 / stp->duty_set;
                stp->alarm_config.flags.auto_reload_on_alarm = true;
                gptimer_set_alarm_action(stp->gptimer, &stp->alarm_config);
                gptimer_start(stp->gptimer);
                ESP_LOGI("SYS", "Duty set to %d", (int)stp->duty_set);

                // increment start curve
                stp->s_cruve_index++;
            }       
            // speeding up
            else {
                // set timer
                stp->duty_set = stp->s_cruve_arr[stp->s_cruve_index] - 1;
                stp->alarm_config.alarm_count = 1000000 / stp->duty_set;
                stp->alarm_config.flags.auto_reload_on_alarm = true;
                gptimer_set_alarm_action(stp->gptimer, &stp->alarm_config);
                ESP_LOGI("SYS", "Duty set to %d", (int)stp->duty_set);

                stp->s_cruve_index++;
            }
        }
    }
}

void _stp_task(void *param) {
    tmc2209_io_t *stp = (tmc2209_io_t *)param;
    while (1) {
        vTaskDelay(1);
        stepper_handle(stp);
    }
}

void stepper_init(tmc2209_io_t *stp) {
    // timer for pulse generation
    {
        gptimer_config_t timer_config = {
            .clk_src = GPTIMER_CLK_SRC_DEFAULT,
            .direction = GPTIMER_COUNT_UP,
            .resolution_hz = 1000000, // 1MHz, 1 tick=1us
        };
        ESP_ERROR_CHECK(gptimer_new_timer(&timer_config, &stp->gptimer));

        gptimer_event_callbacks_t cbs = {
            .on_alarm = example_timer_on_alarm_cb_v1,
        };
        ESP_ERROR_CHECK(gptimer_register_event_callbacks(stp->gptimer, &cbs, stp));

        ESP_ERROR_CHECK(gptimer_enable(stp->gptimer));
    }

    // uart for communication
    {
        uart_config_t uart_config = {
            .baud_rate = 115200,
            .data_bits = UART_DATA_8_BITS,
            .parity = UART_PARITY_DISABLE,
            .stop_bits = UART_STOP_BITS_1,
            .flow_ctrl = UART_HW_FLOWCTRL_DISABLE};

        // Configure UART parameters
        uart_param_config(UART_NUM_2, &uart_config);

        // Configure UART parameters
        uart_param_config(UART_NUM_2, &uart_config);

        // configure pins
        uart_set_pin(UART_NUM_2, stp->tx, stp->rx, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

        // install driver
        uart_driver_install(UART_NUM_2, 512 * 2, 0, 0, NULL, 0);
    }

    stp->alarm_config.alarm_count = 200;
    stp->alarm_config.flags.auto_reload_on_alarm = true;

    stp->duty_set = 0;
    stp->step_position = 0;
    stp->step_target = 0;
    stp->dir_invert = 0;

    xTaskCreate(_stp_task, "_stp_task", 4096, stp, 0, NULL);
}

void stepper_set_invert(tmc2209_io_t *stp, uint8_t inverted) {
    if (stp->duty_set)
        return;

    stp->dir_invert = inverted;
}

void stepper_set_position(tmc2209_io_t *stp, int32_t position) {
    if (stp->duty_set)
        return;
    
    stp->step_position = position;
    stp->step_target = position;
}

void stepper_go_to_pos(tmc2209_io_t *stp, uint32_t rpm_set, int32_t position)
{
    // busy?
    if (stp->duty_set)
        return;
    
    // already there
    if (position == stp->step_position)
        return;

    // higher rpm?
    if (rpm_set > STP_RPM_MAX)
        rpm_set = STP_RPM_MAX;

    // start without curve?
    if (rpm_set <= STP_RPM_DIRECT) {
        stp->s_curve_steps = 1;
    }
    // start with cruve
    else {
        stp->s_curve_steps = rpm_set / 5;
        if (stp->s_curve_steps > S_CURVE_ARR_LEN - 1)
            stp->s_curve_steps = S_CURVE_ARR_LEN - 1;
        if (stp->s_curve_steps < 2)
            stp->s_curve_steps = 2;
    }

    // set start variables
    stp->step_target = position;
    stp->rpm_set = rpm_set;
    stp->s_cruve_index = 0;
    stp->duty_set = 0;

    // which direction?
    if (stp->step_position < stp->step_target) 
        stp->dir_set = 0;
    else
        stp->dir_set = 1;
}

void stepper_stop(tmc2209_io_t *stp)
{
    // not active?
    if (!stp->duty_set)
        return;

    // low rpm?
    if (stp->rpm_set < STP_RPM_DIRECT) {
        if (stp->dir_set)
            stp->step_target = stp->step_position - 1;
        else
            stp->step_target = stp->step_position + 1;
    }
    // not low rpm
    else {
        if (stp->dir_set)
            stp->step_target = stp->step_position - (stp->rpm_set * 25);
        else
            stp->step_target = stp->step_position + (stp->rpm_set * 25);
    }
}

uint8_t stepper_ready(tmc2209_io_t *stp)
{
    if (stp->step_target == stp->step_position)
        return 1;
    else
        return 0;
}

static void swuart_calcCRC(uint8_t *datagram, uint8_t datagramLength)
{
    int i, j;
    uint8_t *crc = datagram + (datagramLength - 1); // CRC located in last byte of message
    uint8_t currentByte;
    *crc = 0;
    for (i = 0; i < (datagramLength - 1); i++)
    {                              // Execute for all bytes of a message
        currentByte = datagram[i]; // Retrieve a byte to be sent from Array
        for (j = 0; j < 8; j++)
        {
            if ((*crc >> 7) ^ (currentByte & 0x01)) // update CRC based result of XOR operation
            {
                *crc = (*crc << 1) ^ 0x07;
            }
            else
            {
                *crc = (*crc << 1);
            }
            currentByte = currentByte >> 1;
        } // for CRC bit
    } // for message byte
}

int stepper_read_reg(tmc2209_io_t *stp, uint8_t reg, uint32_t *data)
{
    int ret = -3;

    for (uint8_t retry = 0; retry < 2; retry++)
    {
        uint8_t tmc_packet[4] = {0x55, 0x00, reg, 0x00};
        swuart_calcCRC(tmc_packet, 4);

        uart_write_bytes(UART_NUM_2, tmc_packet, 4);

        uint8_t buffer[8];

        if (uart_read_bytes(UART_NUM_2, (uint8_t *)buffer, 4, 10) != 4)
        {
            ret = -1;
            ESP_LOGI("RD", "retry %d", ret);
            continue;
        }

        if (uart_read_bytes(UART_NUM_2, (uint8_t *)buffer, 8, 10) != 8)
        {
            ret = -2;
            ESP_LOGI("RD", "retry %d", ret);
            continue;
        }

        uint8_t crc_recv = buffer[7];
        swuart_calcCRC(buffer, 8);

        if (crc_recv != buffer[7])
        {
            ret = -3;
            ESP_LOGI("RD", "retry %d", ret);
            ESP_LOG_BUFFER_HEX("RD", buffer, 8);
            continue;
        }
        else
        {
            *data = buffer[6] | (buffer[5] << 8) | (buffer[4] << 16) | (buffer[3] << 24);
            ret = 0;
            break;
        }
    }

    return ret;
}

int stepper_write_reg(tmc2209_io_t *stp, uint8_t reg, uint32_t data)
{
    int ret = -3;

    for (uint8_t retry = 0; retry < 2; retry++)
    {
        uint8_t tmc_packet[8] = {0x55, 0x00, reg | 0x80, (data >> 24), (data >> 16), (data >> 8), data & 0xFF, 0x00};
        swuart_calcCRC(tmc_packet, 8);

        uart_write_bytes(UART_NUM_2, tmc_packet, 8);

        uint8_t buffer[8];

        if (uart_read_bytes(UART_NUM_2, (uint8_t *)buffer, 8, 10) != 8)
        {
            ret = -1;
            ESP_LOGI("WR", "retry %d", ret);
            continue;
        }

        vTaskDelay(1);

        uint32_t reg_read = 0;
        if (stepper_read_reg(stp, reg, &reg_read) != 0)
        {
            ret = -2;
            ESP_LOGI("WR", "retry %d", ret);
            continue;
        }

        if (reg_read != data)
        {
            ret = -3;
            ESP_LOGI("WR", "retry %d", ret);
            continue;
        }

        ret = 0;
        break;
    }

    return ret;
}