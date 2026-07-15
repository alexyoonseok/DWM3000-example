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
/*
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
    // STEADY STATE: Event Loop
    // =================================================================
    while (1) {
        // Only ONE semaphore take is needed!
        if (xSemaphoreTake(uwb_semaphore, portMAX_DELAY) == pdTRUE) {
            printf("\n⚡ [IRQ EVENT]: DWM3000 signal line toggled! Reading hardware...\n");

            // Explicitly define a clean 5-byte payload to read the Device ID (Register 0x00)
            uint8_t dev_id_tx[5] = { 0x00, 0x00, 0x00, 0x00, 0x00 };
            uint8_t dev_id_rx[5] = { 0 };

            spi_transaction_t id_desc = {
                .length = 5 * 8, // 40 bits
                .tx_buffer = dev_id_tx,
                .rx_buffer = dev_id_rx
            };

            ret = spi_device_transmit(spi_handle, &id_desc);
            if (ret == ESP_OK) {
                uint32_t device_id = (dev_id_rx[1] << 24) | (dev_id_rx[2] << 16) | (dev_id_rx[3] << 8) | dev_id_rx[4];
                printf("Successfully polled device inside RTOS Task. Device ID: 0x%08lX\n", device_id);
            }
        }
    }
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
        .pull_up_en = GPIO_PULLUP_ENABLE, // DWM3000 active low lines hold state high
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE       // Fire interrupt when voltage DROPS (falling edge)
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