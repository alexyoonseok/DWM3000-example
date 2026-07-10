#define PIN_NUM_MISO 19
#define PIN_NUM_MOSI 18
#define PIN_NUM_CLK  5
#define PIN_NUM_CS   14
#define PIN_NUM_IRQ  4

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_master.h" // The native ESP-IDF SPI toolbelt
#include "driver/gpio.h"

void app_main(void) {
    printf("Configuring bare-metal UWB hardware pipeline...\n");

    // =================================================================
    // STEP 1: Configure the physical SPI Bus (The Pin Form)
    // =================================================================
    spi_bus_config_t bus_config = {
        .miso_io_num = PIN_NUM_MISO,
        .mosi_io_num = PIN_NUM_MOSI,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1, // Not used, set to -1
        .quadhd_io_num = -1, // Not used, set to -1
        .max_transfer_sz = 4096
    };

    // Initialize the VSPI host (SPI2_HOST) with our pin form
    // The '0' parameter means we aren't using DMA (Direct Memory Access) for baseline tests
    esp_err_t ret = spi_bus_initialize(SPI2_HOST, &bus_config, 0);
    if (ret == ESP_OK) {
        printf("SPI Bus successfully anchored to copper pins.\n");
    }

    // =================================================================
    // STEP 2: Configure the DWM3000 Device Constraints (The Timing Form)
    // =================================================================
    spi_device_handle_t spi_handle; // This acts as a master key pointer to talk to the chip later
    
    spi_device_interface_config_t dev_config = {
        .clock_speed_hz = 4 * 1000 * 1000,      // Safe baseline testing speed: 4 MHz
        .mode = 0,                             // SPI Mode 0 (CPOL=0, CPHA=0)
        .spics_io_num = PIN_NUM_CS,            // Our designated Chip Select pin
        .queue_size = 7,                       // Allow up to 7 transaction queues in flight
    };

    // Allocate the device onto our initialized bus host
    ret = spi_bus_add_device(SPI2_HOST, &dev_config, &spi_handle);
    if (ret == ESP_OK) {
        printf("DWM3000 timing profile locked into SPI Host controller.\n");
    }

    // =================================================================
    // STEP 3: Read the DWM3000 Device ID to Verify Hardware Connection
    // =================================================================
    
    // Create an empty transaction form
    spi_transaction_t tx_desc = { 0 }; 
    
    // We want to send 1 byte command to READ register 0x00, then read back 4 bytes
    uint8_t tx_buffer[5] = { 0x00 }; // 0x00 is the read command for Device ID register
    uint8_t rx_buffer[5] = { 0 };

    tx_desc.length = 5 * 8;          // Total transaction length in bits (5 bytes * 8 bits)
    tx_desc.tx_buffer = tx_buffer;   // Data we are sending out (MOSI)
    tx_desc.rx_buffer = rx_buffer;   // Buffer to store data coming in (MISO)

    printf("Polling DWM3000 SPI configuration registers...\n");

    // Execute the SPI transfer synchronously
    ret = spi_device_transmit(spi_handle, &tx_desc);
    
    if (ret == ESP_OK) {
        // The first byte received is just turn-around padding. Bytes 1-4 contain the Device ID.
        uint32_t device_id = (rx_buffer[1] << 24) | (rx_buffer[2] << 16) | (rx_buffer[3] << 8) | rx_buffer[4];
        
        printf("Transaction Complete! Raw RX Bytes: 0x%02X 0x%02X 0x%02X 0x%02X\n", rx_buffer[1], rx_buffer[2], rx_buffer[3], rx_buffer[4]);
        printf("Extracted Device ID: 0x%08lX\n", device_id);
        
        // Qorvo DWM3000 chips typically return a value starting with 0xDECA03xx
        if ((device_id & 0xFFFF0000) == 0xDECA0000 || device_id != 0) {
            printf("SUCCESS: Hardware communication established. DWM3000 is alive!\n");
        } else {
            printf("ERROR: Received invalid data. Check your jumper wiring connections.\n");
        }
    } else {
        printf("SPI Transaction Failed to execute.\n");
    }

    // =================================================================
    // STEP 4: The Continuous Background FreeRTOS Keep-Alive
    // =================================================================
    while(1) {
        // Keep the CPU core happy and breathing
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}