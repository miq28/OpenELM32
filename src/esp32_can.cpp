#include "esp32_can.h"
#include "rgb_status.h"

const char *alertToStr(uint32_t a)
{
    switch (a)
    {
    case TWAI_ALERT_TX_IDLE:
        return "TX_IDLE";
    case TWAI_ALERT_TX_SUCCESS:
        return "TX_SUCCESS";
    case TWAI_ALERT_RX_DATA:
        return "RX_DATA";
    case TWAI_ALERT_BELOW_ERR_WARN:
        return "BELOW_ERR_WARN";
    case TWAI_ALERT_ERR_ACTIVE:
        return "ERR_ACTIVE";
    case TWAI_ALERT_RECOVERY_IN_PROGRESS:
        return "RECOVERY_IN_PROGRESS";
    case TWAI_ALERT_BUS_RECOVERED:
        return "BUS_RECOVERED";
    case TWAI_ALERT_ARB_LOST:
        return "ARB_LOST";
    case TWAI_ALERT_ABOVE_ERR_WARN:
        return "ABOVE_ERR_WARN";
    case TWAI_ALERT_BUS_ERROR:
        return "BUS_ERROR";
    case TWAI_ALERT_TX_FAILED:
        return "TX_FAILED";
    case TWAI_ALERT_RX_QUEUE_FULL:
        return "RX_QUEUE_FULL";
    case TWAI_ALERT_ERR_PASS:
        return "ERR_PASSIVE";
    case TWAI_ALERT_BUS_OFF:
        return "BUS_OFF";
    case TWAI_ALERT_RX_FIFO_OVERRUN:
        return "RX_FIFO_OVERRUN";
    case TWAI_ALERT_TX_RETRIED:
        return "TX_RETRIED";
    case TWAI_ALERT_PERIPH_RESET:
        return "PERIPH_RESET";
    default:
        return "UNKNOWN";
    }
}

const char *stateToStr(uint8_t s)
{
    switch (s)
    {
    case TWAI_STATE_STOPPED:
        return "STOPPED";
    case TWAI_STATE_RUNNING:
        return "RUNNING";
    case TWAI_STATE_BUS_OFF:
        return "BUS_OFF";
    case TWAI_STATE_RECOVERING:
        return "RECOVERING";
    default:
        return "?";
    }
}

void alertsToText(uint32_t alerts, char *out, size_t len)
{
    out[0] = 0;

#define ADD_FLAG(flag, name)     \
    if (alerts & flag)           \
    {                            \
        strlcat(out, name, len); \
        strlcat(out, "|", len);  \
    }

    ADD_FLAG(TWAI_ALERT_BUS_ERROR, "BUS_ERROR");
    ADD_FLAG(TWAI_ALERT_TX_FAILED, "TX_FAILED");
    ADD_FLAG(TWAI_ALERT_RX_QUEUE_FULL, "RX_FULL");
    ADD_FLAG(TWAI_ALERT_BUS_OFF, "BUS_OFF");
    ADD_FLAG(TWAI_ALERT_ERR_PASS, "ERR_PASSIVE");
    ADD_FLAG(TWAI_ALERT_BUS_RECOVERED, "RECOVERED");
    ADD_FLAG(TWAI_ALERT_RECOVERY_IN_PROGRESS, "RECOVERING");

#undef ADD_FLAG

    // remove trailing '|'
    size_t l = strlen(out);
    if (l > 0 && out[l - 1] == '|')
        out[l - 1] = 0;
}

ESP32CAN CAN0(GPIO_NUM_16, GPIO_NUM_17, 0);
ESP32CAN CAN1(GPIO_NUM_18, GPIO_NUM_19, 1);

ESP32CAN::ESP32CAN(gpio_num_t rxPin, gpio_num_t txPin, uint8_t)
{
    g_config = TWAI_GENERAL_CONFIG_DEFAULT(txPin, rxPin, TWAI_MODE_NORMAL);
    g_config.tx_queue_len = 16;
    g_config.rx_queue_len = 256;
    g_config.alerts_enabled =
        TWAI_ALERT_ERR_PASS |
        TWAI_ALERT_BUS_ERROR |
        TWAI_ALERT_RX_QUEUE_FULL |
        TWAI_ALERT_TX_FAILED |
        TWAI_ALERT_BUS_OFF |
        TWAI_ALERT_BUS_RECOVERED |
        TWAI_ALERT_RECOVERY_IN_PROGRESS;
    f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();
}

uint32_t ESP32CAN::begin(uint32_t baudrate)
{
    currentBaudrate = baudrate;

    switch (baudrate)
    {
    case 1000000:
        t_config = TWAI_TIMING_CONFIG_1MBITS();
        break;
    case 500000:
        t_config = TWAI_TIMING_CONFIG_500KBITS();
        break;
    case 250000:
        t_config = TWAI_TIMING_CONFIG_250KBITS();
        break;
    case 125000:
        t_config = TWAI_TIMING_CONFIG_125KBITS();
        break;
    default:
        return 0;
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

bool ESP32CAN::sendFrame(CAN_FRAME &frame)
{
    twai_message_t msg = {};
    msg.identifier = frame.id;
    msg.data_length_code = frame.length;
    msg.extd = frame.extended;
    msg.rtr = frame.rtr;

    memcpy(msg.data, frame.data.uint8, frame.length);

    bool ok = (twai_transmit(&msg, pdMS_TO_TICKS(10)) == ESP_OK);

    if (ok)
    {
        rgbCanTxActivity();
    }

    return ok;
}

uint32_t ESP32CAN::read(CAN_FRAME &frame)
{
    twai_message_t msg;

    if (twai_receive(&msg, 0) == ESP_OK)
    {
        rgbCanRxActivity();

        frame.id = msg.identifier;
        frame.length = msg.data_length_code;
        frame.extended = msg.extd;
        frame.rtr = msg.rtr;

        memcpy(frame.data.uint8, msg.data, msg.data_length_code);
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

bool ESP32CAN::isInErrorState()
{
    twai_status_info_t status;

    if (twai_get_status_info(&status) != ESP_OK)
        return false;

    return (
        status.state == TWAI_STATE_BUS_OFF ||
        status.state == TWAI_STATE_RECOVERING ||
        status.state == TWAI_STATE_STOPPED ||
        status.tx_error_counter >= 128 ||
        status.rx_error_counter >= 128);
}

void ESP32CAN::setListenOnlyMode(bool state)
{
    disable();

    g_config.mode =
        state
            ? TWAI_MODE_LISTEN_ONLY
            : TWAI_MODE_NORMAL;

    begin(currentBaudrate);
}

void ESP32CAN::setCANPins(gpio_num_t rxPin, gpio_num_t txPin)
{
    g_config.rx_io = rxPin;
    g_config.tx_io = txPin;
}

// =========== EVENT API ============
inline void ESP32CAN::pushEvent(const CAN_EVENT &evt)
{
    uint16_t next = (eventHead + 1) % EVENT_BUFFER_SIZE;

    if (next == eventTail)
    {
        // drop oldest (never block)
        eventTail = (eventTail + 1) % EVENT_BUFFER_SIZE;
    }

    eventBuffer[eventHead] = evt;
    eventHead = next;
}

void ESP32CAN::pollEvents()
{
    uint32_t alerts;

    static uint32_t lastAlerts = 0;
    static uint32_t lastReportTime = 0;

    uint32_t now = millis();

    if (twai_read_alerts(&alerts, 0) == ESP_OK && alerts)
    {
        // suppress identical spam within window
        if (alerts == lastAlerts && (now - lastReportTime) < 100)
            return;

        lastAlerts = alerts;
        lastReportTime = now;

        twai_status_info_t status;
        twai_get_status_info(&status);

        CAN_EVENT evt;
        evt.timestamp = millis();
        evt.alerts = alerts;
        evt.rx_err = status.rx_error_counter;
        evt.tx_err = status.tx_error_counter;
        evt.state = status.state;

        pushEvent(evt);
    }
}

bool ESP32CAN::popEvent(CAN_EVENT &evt)
{
    if (eventTail == eventHead)
        return false;

    evt = eventBuffer[eventTail];
    eventTail = (eventTail + 1) % EVENT_BUFFER_SIZE;
    return true;
}