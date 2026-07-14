#define PIN_NUM_CLK  5   
#define PIN_NUM_MOSI 18  
#define PIN_NUM_MISO 19  
#define PIN_NUM_CS   14  
#define PIN_NUM_IRQ  4   // Labeled "4" on HUZZAH32 -> Connects to "D2" on Shield

#include <stdio.h>
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
    uint8_t tx_buffer[5] = { 0x00 }; 
    uint8_t rx_buffer[5] = { 0 };

    while (1) {
        // Sleep indefinitely until the ISR unlatches the semaphore
        if (xSemaphoreTake(uwb_semaphore, portMAX_DELAY) == pdTRUE) {
            printf("\n⚡ [IRQ EVENT]: DWM3000 signal line toggled! Reading hardware...\n");

            spi_transaction_t tx_desc = {
                .length = 5 * 8,
                .tx_buffer = tx_buffer,
                .rx_buffer = rx_buffer
            };

            esp_err_t ret = spi_device_transmit(spi_handle, &tx_desc);
            if (ret == ESP_OK) {
                uint32_t device_id = (rx_buffer[1] << 24) | (rx_buffer[2] << 16) | (rx_buffer[3] << 8) | rx_buffer[4];
                printf("Successfully polled device inside RTOS Task. Device ID: 0x%08X\n", device_id);
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