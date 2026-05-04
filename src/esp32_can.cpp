#include "esp32_can.h"

ESP32CAN::ESP32CAN(gpio_num_t rxPin, gpio_num_t txPin, uint8_t)
{
    g_config = TWAI_GENERAL_CONFIG_DEFAULT(txPin, rxPin, TWAI_MODE_NORMAL);
    g_config.tx_queue_len = 16;
    g_config.rx_queue_len = 64;
    f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();
}

uint32_t ESP32CAN::begin(uint32_t baudrate)
{
    switch (baudrate)
    {
        case 1000000: t_config = TWAI_TIMING_CONFIG_1MBITS(); break;
        case 500000:  t_config = TWAI_TIMING_CONFIG_500KBITS(); break;
        case 250000:  t_config = TWAI_TIMING_CONFIG_250KBITS(); break;
        case 125000:  t_config = TWAI_TIMING_CONFIG_125KBITS(); break;
        default: return 0;
    }

    twai_driver_install(&g_config, &t_config, &f_config);
    twai_start();
    return baudrate;
}

void ESP32CAN::disable()
{
    twai_stop();
    twai_driver_uninstall();
}

bool ESP32CAN::sendFrame(CAN_FRAME& frame)
{
    twai_message_t msg = {};
    msg.identifier = frame.id;
    msg.data_length_code = frame.length;
    msg.extd = frame.extended;
    msg.rtr = frame.rtr;

    memcpy(msg.data, frame.data.byte, frame.length);

    return twai_transmit(&msg, pdMS_TO_TICKS(10)) == ESP_OK;
}

uint32_t ESP32CAN::get_rx_buff(CAN_FRAME &frame)
{
    twai_message_t msg;

    if (twai_receive(&msg, 0) == ESP_OK)
    {
        frame.id = msg.identifier;
        frame.length = msg.data_length_code;
        frame.extended = msg.extd;
        frame.rtr = msg.rtr;

        memcpy(frame.data.byte, msg.data, msg.data_length_code);
        return 1;
    }
    return 0;
}

uint16_t ESP32CAN::available()
{
    twai_status_info_t status;
    if (twai_get_status_info(&status) == ESP_OK)
        return status.msgs_to_rx;
    return 0;
}

void ESP32CAN::setListenOnlyMode(bool state)
{
    disable();
    g_config.mode = state ? TWAI_MODE_LISTEN_ONLY : TWAI_MODE_NORMAL;
    begin(500000);
}

void ESP32CAN::setCANPins(gpio_num_t rxPin, gpio_num_t txPin)
{
    g_config.rx_io = rxPin;
    g_config.tx_io = txPin;
}