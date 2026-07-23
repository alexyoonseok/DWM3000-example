#define PIN_NUM_CLK  5   
#define PIN_NUM_MOSI 18  
#define PIN_NUM_MISO 19  
#define PIN_NUM_CS   14  
#define PIN_NUM_IRQ  4   // Labeled "4" on HUZZAH32 -> Connects to "D2" on Shield

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h" // FreeRTOS synchronization toolkit
#include "driver/spi_master.h"
#include "driver/gpio.h"

// Master pointer keys for handling background task assets
spi_device_handle_t spi_handle;
SemaphoreHandle_t uwb_semaphore = NULL;

// Forward declaration
void send_poll_frame(void);

// =================================================================
// REGISTER READBACK VERIFIER
// =================================================================
void debug_read_register(const char* reg_name, uint8_t base_addr, uint8_t sub_addr, uint8_t data_len) {
    uint8_t tx_buf[10] = { 0 };
    uint8_t rx_buf[10] = { 0 };

    tx_buf[0] = 0x40 | ((base_addr & 0x1F) << 1) | ((sub_addr >> 6) & 0x01);
    tx_buf[1] = (sub_addr & 0x3F) << 2;

    spi_transaction_t desc = {
        .length = (2 + data_len) * 8,
        .tx_buffer = tx_buf,
        .rx_buffer = rx_buf
    };

    if (spi_device_transmit(spi_handle, &desc) == ESP_OK) {
        printf(" [VERIFY %s (REG:%02X:%02X)]: ", reg_name, base_addr, sub_addr);
        for (int i = 2; i < 2 + data_len; i++) {
            printf("0x%02X ", rx_buf[i]);
        }
        printf("\n");
    }
}


// =================================================================
// 1. THE HARDWARE INTERRUPT SERVICE ROUTINE (ISR)
// =================================================================
// This function runs on raw silicon level when Pin 4 drops or rises.
static void IRAM_ATTR uwb_gpio_isr_handler(void* arg) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    
    // Unlatch the semaphore to instantly wake up the background task
    xSemaphoreGiveFromISR(uwb_semaphore, &xHigherPriorityTaskWoken);
    
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR(); // Force CPU to switch context directly to our task
    }
}

// =================================================================
// 2. THE BACKGROUND RTOS WORKER TASK
// =================================================================
void uwb_event_processor_task(void *pvParameters) {
    printf("UWB Asynchronous Worker Task successfully initialized.\n");
    esp_err_t ret;

/* Uncomment SYS_STATUS reading if needed in the future
    // =================================================================
    // VERIFICATION: Read the System Status Register (0x00:44)
    // =================================================================
    printf("Polling DWM3000 System Status Register...\n");

    // 6 bytes total: 2 for the Extended Header, and 4 for the 32-bit status data
    uint8_t status_tx[6] = { 0x41, 0x10, 0x00, 0x00, 0x00, 0x00 };
    uint8_t status_rx[6] = { 0 };

    spi_transaction_t status_desc = {
        .length = 6 * 8, // 48 bits total
        .tx_buffer = status_tx,
        .rx_buffer = status_rx
    };

    ret = spi_device_transmit(spi_handle, &status_desc);
    
    if (ret == ESP_OK) {
        // rx[0] and rx[1] are garbage (absorbed while sending the headers)
        // rx[2] through rx[5] contain the actual 32-bit SYS_STATUS payload
        printf("System Status Register Bytes: 0x%02X 0x%02X 0x%02X 0x%02X\n", 
               status_rx[2], status_rx[3], status_rx[4], status_rx[5]);
        
        // Reconstruct the 32-bit integer (Little-Endian formatting)
        uint32_t sys_status_val = (status_rx[5] << 24) | (status_rx[4] << 16) | (status_rx[3] << 8) | status_rx[2];
        printf("Complete SYS_STATUS Value: 0x%08lX\n", sys_status_val);
    }
*/



/* Uncomment CRCE cleanup if needed in the future.
    // =================================================================
    // CLEANUP: Clear the Startup SPI CRC Error Flag (Write-1-to-Clear)
    // =================================================================
    printf("Clearing startup SPI CRC error flag...\n");

    // Byte 0: Write (1), Extended (1), Base 0x00, Sub-bit[6] (1) -> 0xC1
    // Byte 1: Sub-bits[5:0] for Offset 0x44, Mode Bits (00)     -> 0x10
    // Byte 2: Data payload writing a 1 strictly to Bit 2       -> 0x04
    uint8_t clear_tx[3] = { 0xC1, 0x10, 0x04 };
    uint8_t clear_rx[3] = { 0 };

    spi_transaction_t clear_desc = {
        .length = 3 * 8, // 24 bits
        .tx_buffer = clear_tx,
        .rx_buffer = clear_rx
    };

    ret = spi_device_transmit(spi_handle, &clear_desc);
    if (ret == ESP_OK) {
        printf("Clear command pushed. Re-polling status register...\n");
    }

    // Perform a quick follow-up read to verify the flag dropped
    ret = spi_device_transmit(spi_handle, &status_desc);
    if (ret == ESP_OK) {
        uint32_t clean_status = (status_rx[5] << 24) | (status_rx[4] << 16) | (status_rx[3] << 8) | status_rx[2];
        printf("New Baseline SYS_STATUS Value: 0x%08lX\n", clean_status);
    }
*/

    // =================================================================
    // AUTO IDLE PLL CONFIGURATION: Safe 16-Bit Masked Write on SEQ_CTRL (REG:11:08)
    // =================================================================
    printf("Enabling AINIT2IDLE via 16-bit hardware mask (REG:11:08)...\n");

    // Header: 0xE2, 0x22 (Base 0x11, Sub 0x08, Mode 10 for 16-bit mask)
    // AND Mask: 0xFF, 0xFF (Preserve all existing register states)
    // OR Mask:  0x00, 0x01 (Force Bit 8 [AINIT2IDLE] to 1)
    uint8_t seq_mask_tx[6] = { 0xE2, 0x22, 0xFF, 0xFF, 0x00, 0x01 };

    spi_transaction_t seq_desc = {
        .length = 6 * 8, // 48 bits total
        .tx_buffer = seq_mask_tx,
        .rx_buffer = NULL
    };

    ret = spi_device_transmit(spi_handle, &seq_desc);
    if (ret == ESP_OK) {
        printf(" -> SEQ_CTRL updated safely! AINIT2IDLE (Bit 8) enabled without touching reserved bits.\n");
        debug_read_register("AINIT2IDLE", 0x11, 0x08, 4);
    }
    else {
        printf(" * Failed to enable AINIT2IDLE");
    }


    // =================================================================
    // 1. RF CONFIGURATION: Establish Channel 5 @ 64 MHz PRF
    // =================================================================
    printf("Writing Channel Control Profiles (REG:01:14)...\n");

    // Base 0x01, Sub 0x14
    // 2 Header Bytes + 2 Data Bytes (0x094E50C2 in Little-Endian)
    uint8_t chan_config_tx[4] = { 0xC2, 0x50, 0x4E, 0x09 };
    uint8_t chan_config_rx[4] = { 0 };

    spi_transaction_t chan_desc = {
        .length = 4 * 8, // 48 bits
        .tx_buffer = chan_config_tx,
        .rx_buffer = chan_config_rx
    };

    ret = spi_device_transmit(spi_handle, &chan_desc);
    if (ret == ESP_OK) {
        printf(" -> Channel 5 profile and Preamble Code 9 committed successfully.\n");
        debug_read_register("CHAN_CTRL", 0x01, 0x14, 2);
    }
    else {
        printf(" * Failed to configure CHAN_CTRL register");
    }

    // =================================================================
    // 2. FRAME CONFIGURATION: Set 512 Preamble Length & 6.8 Mbps Data Rate
    // =================================================================
    printf("Writing Transmit Frame Control Parameters (REG:00:24)...\n");

    // 2 Header Bytes + 6 Data Bytes (0x3F0000000C0C90C0 in Little-Endian)
    uint8_t frame_config_tx[8] = { 0xC0, 0x90, 0x0C, 0x0C, 0x00, 0x00, 0x00, 0x3F };
    uint8_t frame_config_rx[8] = { 0 };

    spi_transaction_t frame_desc = {
        .length = 8 * 8, // 48 bits
        .tx_buffer = frame_config_tx,
        .rx_buffer = frame_config_rx
    };

    ret = spi_device_transmit(spi_handle, &frame_desc);
    if (ret == ESP_OK) {
        printf(" -> Preamble length targeted to 512 symbols at 6.8 Mbps.\n");
        debug_read_register("TX_FCTRL", 0x00, 0x24, 6);
    }
    else {
        printf(" * Failed to configure TX_FCTRL register");
    }

    // =================================================================
    // 3. ANTENNA DELAY CONFIGURATION: Set Default Calibration Values
    // =================================================================
    printf("Setting Antenna Delays (REG:01:04 & REG:0E:00)...\n");

    // 1. TX Antenna Delay (REG:01:04) -> 2 Header Bytes + 2 Data Bytes (0x4015)
    // Header: 0xC2 (Base 0x01, WR), 0x10 (Sub 0x04)
    uint8_t tx_antd_tx[4] = { 0xC2, 0x10, 0x15, 0x40 };
    spi_transaction_t tx_antd_desc = { 
        .length = 4 * 8, 
        .tx_buffer = tx_antd_tx, 
        .rx_buffer = NULL 
    };
    spi_device_transmit(spi_handle, &tx_antd_desc);
    debug_read_register("TX_ANTD", 0x01, 0x04, 2);

    // 2. RX Antenna Delay inside CIA_CONF (REG:0E:00) -> 2 Header Bytes + 4 Data Bytes
    // Header: 0xDC (Base 0x0E, WR), 0x00 (Sub 0x00)
    // Payload: Lower 16 bits = 0x4015 (RX_ANTD), Bit 20 = 1 (MINDIAG) -> 0x00104015
    uint8_t cia_conf_tx[6] = { 0xDC, 0x00, 0x15, 0x40, 0x10, 0x00 };
    spi_transaction_t cia_conf_desc = { 
        .length = 6 * 8, 
        .tx_buffer = cia_conf_tx, 
        .rx_buffer = NULL 
    };
    spi_device_transmit(spi_handle, &cia_conf_desc);
    debug_read_register("CIA_CONF", 0x0E, 0x00, 4);

    printf(" -> Antenna delays set to 0x4015 with MINDIAG enabled.\n");

    // =================================================================
    // 4. INTERRUPT MASK: 16-Bit Hardware Masked Write on SYS_ENABLE_0 
    // =================================================================
    printf("Unmasking TXFRS and RXFCG via Hardware Masked Write (REG:00:3C)...\n");

    // Header: 0xC0 (Write, Base 0), 0xF2 (Sub 0x3C, Mode 10 for 16-bit mask)
    // AND Mask: 0xFF, 0xFF (Preserve all existing bit states)
    // OR Mask:  0x80, 0x40 (Force Bit 7 and Bit 14 to 1)
    
    uint8_t enable_mask_tx[6] = { 0xC0, 0xF2, 0xFF, 0xFF, 0x80, 0x40 };
    
    spi_transaction_t mask_enable_desc = {
        .length = 6 * 8, // 48 bits total
        .tx_buffer = enable_mask_tx,
        .rx_buffer = NULL
    };

    ret = spi_device_transmit(spi_handle, &mask_enable_desc);
    if (ret == ESP_OK) {
        printf(" -> SYS_ENABLE_0 updated via 16-bit hardware mask!\n");
        debug_read_register("SYS_ENABLE_0", 0x00, 0x3C, 6);
    }
    else {
        printf(" * Failed to unmask interrupt masks");
    }

    // =================================================================
    // STARTUP CLEANUP: Clear power-on flags in SYS_STATUS (W1C)
    // =================================================================
    printf("Clearing startup status flags to drive IRQ pin LOW...\n");

    // Write-1-to-Clear (W1C) on SYS_STATUS (0x00:44)
    // Header: 0xC1, 0x10. Write 0xFF to clear all lower bits (including IRQS and SPIRDY)
    uint8_t clear_startup_tx[8] = { 0xC1, 0x10, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    
    spi_transaction_t clear_startup_desc = {
        .length = 8 * 8,
        .tx_buffer = clear_startup_tx,
        .rx_buffer = NULL
    };
    spi_device_transmit(spi_handle, &clear_startup_desc);

    // Verify SYS_STATUS dropped to 0x00
    debug_read_register("SYS_STATUS (Post-Clear)", 0x00, 0x44, 6);

    vTaskDelay(10 / portTICK_PERIOD_MS);

    // =================================================================
    // STEADY STATE: Event Loop
    // =================================================================
    while (1) {
        printf("\n[TIMER]: Sending periodic POLL frame...\n");
        send_poll_frame();

        // Wait up to 1 second for the hardware IRQ rising edge
        if (xSemaphoreTake(uwb_semaphore, pdMS_TO_TICKS(1000)) == pdTRUE) {
            printf("⚡ [HW IRQ RECEIVED]: DW3000 fired rising edge on GPIO 4!\n");

            // Read 8 bytes total: 2 Header Bytes + 6 Data Octets
            uint8_t status_tx[8] = { 0x41, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
            uint8_t status_rx[8] = { 0 };
            spi_transaction_t status_desc = { .length = 8 * 8, .tx_buffer = status_tx, .rx_buffer = status_rx };
            spi_device_transmit(spi_handle, &status_desc);

            // Print ALL 6 payload bytes (Octets 0 through 5)
            printf(" -> Full SYS_STATUS: 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X\n", 
                status_rx[2], status_rx[3], status_rx[4], status_rx[5], status_rx[6], status_rx[7]);

            // Check if Bit 7 of Byte 0 (TXFRS) or any RX event in Byte 1 / Byte 4 fired
            if (status_rx[2] & 0x80) {
                printf(" 🎉 SUCCESS: [TX COMPLETE] POLL frame sent over the air!\n");
            }

            // Write-1-to-Clear (W1C) across ALL 6 bytes to clear Octets 4 and 5 as well!
            uint8_t clear_tx[8] = {
                0xC1, 0x10,
                status_rx[2], status_rx[3], status_rx[4],
                status_rx[5], status_rx[6], status_rx[7]
            };
            spi_transaction_t clear_desc = { .length = 8 * 8, .tx_buffer = clear_tx, .rx_buffer = NULL };
            spi_device_transmit(spi_handle, &clear_desc);
        }

        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}


// =================================================================
// TAG TRANSMITTER: Send SS-TWR Poll Frame (DW3000 Native)
// =================================================================

// 12-byte payload: "POLL_FRAME_0"
static uint8_t poll_payload[12] = { 'P', 'O', 'L', 'L', '_', 'F', 'R', 'A', 'M', 'E', '_', '0' };

void send_poll_frame(void) {
    printf("\n[TAG]: Preparing to transmit POLL frame...\n");

    // -------------------------------------------------------------
    // 1. Write Payload to TX_BUFFER (Base Address 0x14, Sub-Offset 0x00)
    // -------------------------------------------------------------
    // Header for Full Addressed Write to 0x14:00 -> { 0xE8, 0x00 }
    // (Base 0x14: WR=1, Full=1, Base=10100, Sub6=0 -> 1110 1000 = 0xE8)
    uint8_t tx_buf_spi[14];
    tx_buf_spi[0] = 0xE8; 
    tx_buf_spi[1] = 0x00;
    memcpy(&tx_buf_spi[2], poll_payload, 12);

    spi_transaction_t tx_buf_desc = {
        .length = 14 * 8, // 2 Header Bytes + 12 Payload Bytes
        .tx_buffer = tx_buf_spi,
        .rx_buffer = NULL
    };
    spi_device_transmit(spi_handle, &tx_buf_desc);

    // -------------------------------------------------------------
    // 2. Update TXFLEN in TX_FCTRL (Base 0x00, Sub-Offset 0x24)
    // -------------------------------------------------------------
    // Total Frame Length = 12 Payload Bytes + 2 CRC FCS Bytes = 14 Bytes (0x000E)
    // Use 16-bit Hardware Masked Write to update lower 10 bits (TXFLEN)
    // Header for Masked Write to 0x00:24 -> { 0xC0, 0x92 } (Base 0, Sub 0x24, Mode 10)
    // AND Mask: 0xFC00 (Preserve upper bits), OR Mask: 0x000E (TXFLEN = 14)
    uint8_t fctrl_mask_tx[6] = { 0xC0, 0x92, 0x00, 0xFC, 0x0E, 0x00 };

    spi_transaction_t fctrl_desc = {
        .length = 6 * 8,
        .tx_buffer = fctrl_mask_tx,
        .rx_buffer = NULL
    };
    spi_device_transmit(spi_handle, &fctrl_desc);

    // Short 1ms delay for register latches to settle before firing command
    vTaskDelay(1 / portTICK_PERIOD_MS);

    // -------------------------------------------------------------
    // 3. Trigger Transmission + Wait-for-Response (CMD_TX_W4R = 0x0C)
    // -------------------------------------------------------------
    // Fast Command Header for 0x0C: 1 0 [01100] 1 -> 0x99
    uint8_t fast_cmd_tx[1] = { 0x83 };
    spi_transaction_t cmd_desc = {
        .length = 1 * 8,
        .tx_buffer = fast_cmd_tx,
        .rx_buffer = NULL
    };
    spi_device_transmit(spi_handle, &cmd_desc);

    printf(" -> CMD_TX issued! Radio transmitted and auto-enabled receiver.\n");
}

void app_main(void) {
    printf("Initializing Real-Time Operating System Environment...\n");

    // Initialize FreeRTOS synchronization tools
    uwb_semaphore = xSemaphoreCreateBinary();

    // Configure SPI Bus & Device Timing Constraints
    spi_bus_config_t bus_config = {
        .miso_io_num = PIN_NUM_MISO,
        .mosi_io_num = PIN_NUM_MOSI,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1, .quadhd_io_num = -1,
        .max_transfer_sz = 4096
    };
    spi_bus_initialize(SPI2_HOST, &bus_config, 0);

    spi_device_interface_config_t dev_config = {
        .clock_speed_hz = 4 * 1000 * 1000,
        .mode = 0,
        .spics_io_num = PIN_NUM_CS,
        .queue_size = 7,
    };
    spi_bus_add_device(SPI2_HOST, &dev_config, &spi_handle);

    // =================================================================
    // STEP 3: Configure GPIO Interrupt Architecture
    // =================================================================
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << PIN_NUM_IRQ),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_POSEDGE       // Fire interrupt when voltage DROPS (falling edge)
    };
    gpio_config(&io_conf);

    // Install the global interrupt controller service package on the CPU core
    gpio_install_isr_service(0);
    // Hook our specific pin handler function into the hardware vector line
    gpio_isr_handler_add(PIN_NUM_IRQ, uwb_gpio_isr_handler, NULL);

    // =================================================================
    // STEP 4: Spawn the Asynchronous Thread Task onto FreeRTOS
    // =================================================================
    xTaskCreatePinnedToCore(
        uwb_event_processor_task,   // Function block pointer name
        "UWB_Task",                // Internal debug text label name
        4096,                      // Stack memory size allocated in bytes
        NULL,                      // Parameters passed to task
        3,                         // Priority tier level (Higher numbers run first!)
        NULL,                      // Task handle storage pointer
        1                          // Pin this task explicitly to CPU Core 1
    );

    printf("Main setup complete. Handing resource scheduling to FreeRTOS system core.\n");
    
    while(1) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}