#include "uart_sender.h"

#include <atomic>

#include "config.h"
#include <driver/uart.h>

namespace {

constexpr uart_port_t kSenderUartPort = UART_NUM_2;
std::atomic<uint32_t> s_current_baudrate{UART_BAUDRATE};
std::atomic<bool> s_uart_ready{false};

bool uart_sender_apply_config(uint32_t baudrate) {
    uart_config_t uart_config = {};
    uart_config.baud_rate = (int)baudrate;
    uart_config.data_bits = UART_DATA_8_BITS;
    uart_config.parity = UART_PARITY_DISABLE;
    uart_config.stop_bits = UART_STOP_BITS_1;
    uart_config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    uart_config.rx_flow_ctrl_thresh = 0;
#if defined(UART_SCLK_DEFAULT)
    uart_config.source_clk = UART_SCLK_DEFAULT;
#endif

    const esp_err_t param_err = uart_param_config(kSenderUartPort, &uart_config);
    if (param_err != ESP_OK) {
        return false;
    }

    const int rx_pin = (UART_RX_PIN >= 0) ? UART_RX_PIN : UART_PIN_NO_CHANGE;
    const esp_err_t pin_err = uart_set_pin(
        kSenderUartPort,
        UART_TX_PIN,
        rx_pin,
        UART_PIN_NO_CHANGE,
        UART_PIN_NO_CHANGE
    );
    if (pin_err != ESP_OK) {
        return false;
    }

    return true;
}

} // namespace

void uart_sender_init() {
    uart_driver_delete(kSenderUartPort);

    const esp_err_t install_err = uart_driver_install(kSenderUartPort, 256, 0, 0, nullptr, 0);
    if (install_err != ESP_OK) {
        s_uart_ready.store(false);
        return;
    }

    if (!uart_sender_apply_config(UART_BAUDRATE)) {
        uart_driver_delete(kSenderUartPort);
        s_uart_ready.store(false);
        return;
    }

    s_current_baudrate.store(UART_BAUDRATE);
    s_uart_ready.store(true);
}

bool uart_sender_set_baudrate(uint32_t baudrate) {
    if (baudrate < UART_BAUDRATE_MIN || baudrate > UART_BAUDRATE_MAX) {
        return false;
    }

    if (!s_uart_ready.load()) {
        return false;
    }

    const esp_err_t err = uart_set_baudrate(kSenderUartPort, baudrate);
    if (err != ESP_OK) {
        return false;
    }

    s_current_baudrate.store(baudrate);
    return true;
}

uint32_t uart_sender_get_baudrate() {
    return s_current_baudrate.load();
}

bool uart_sender_is_ready() {
    return s_uart_ready.load();
}

size_t uart_sender_write(const uint8_t* data, size_t len) {
    if (!s_uart_ready.load() || data == nullptr || len == 0) {
        return 0;
    }

    const int written = uart_write_bytes(kSenderUartPort, data, len);
    if (written < 0) {
        return 0;
    }
    return (size_t)written;
}

void uart_sender_flush() {
    if (!s_uart_ready.load()) {
        return;
    }
    uart_wait_tx_done(kSenderUartPort, pdMS_TO_TICKS(1000));
}
