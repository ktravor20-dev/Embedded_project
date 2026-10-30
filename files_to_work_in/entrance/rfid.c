#include "smart_cocis.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
static spi_device_handle_t readers[2];
static bool reader_ready[2];
static uint8_t last_irq[2];
static uint8_t last_error[2];
static int last_received[2];
static int64_t next_report[2];
/* Write one MFRC522 register through its own SPI chip-select handle. */
static void write_register(int reader, uint8_t address, uint8_t value)
{
    spi_transaction_t transaction = { .length = 16, .flags = SPI_TRANS_USE_TXDATA };
    transaction.tx_data[0] = (address << 1) & 0x7e;
    transaction.tx_data[1] = value;
    ESP_ERROR_CHECK(spi_device_transmit(readers[reader], &transaction));
}
/* Read one MFRC522 register using an atomic SPI transaction. */
static uint8_t read_register(int reader, uint8_t address)
{
    spi_transaction_t transaction = { .length = 16, .flags = SPI_TRANS_USE_TXDATA | SPI_TRANS_USE_RXDATA };
    transaction.tx_data[0] = ((address << 1) & 0x7e) | 0x80;
    ESP_ERROR_CHECK(spi_device_transmit(readers[reader], &transaction));
    return transaction.rx_data[1];
}
/* Write a complete card command into the FIFO while keeping chip select low. */
static void write_fifo(int reader, const uint8_t *data, int length)
{
    uint8_t transmit[17] = {0};
    configASSERT(length > 0 && length <= 16);
    transmit[0] = 0x12;
    memcpy(transmit + 1, data, length);
    spi_transaction_t transaction = {
        .length = (length + 1) * 8, .tx_buffer = transmit
    };
    ESP_ERROR_CHECK(spi_device_transmit(readers[reader], &transaction));
}
/* Read the full FIFO response in one SPI transfer, ending with a zero address byte. */
static void read_fifo(int reader, uint8_t *data, int length)
{
    uint8_t transmit[17] = {0};
    uint8_t receive[17] = {0};
    configASSERT(length > 0 && length <= 16);
    for (int index = 0; index < length; index++) transmit[index] = 0x92;
    spi_transaction_t transaction = {
        .length = (length + 1) * 8, .tx_buffer = transmit, .rx_buffer = receive
    };
    ESP_ERROR_CHECK(spi_device_transmit(readers[reader], &transaction));
    memcpy(data, receive + 1, length);
}
/* Report the failed reader stage at most once every five seconds per reader. */
static void report_reader_stage(int reader, const char *stage)
{
    if (now_ms() < next_report[reader]) return;
    next_report[reader] = now_ms() + 5000;
    ESP_LOGW("RFID", "%s: %s; version=%02X antenna=%02X IRQ=%02X error=%02X bytes=%d",
        reader == 0 ? "ENTRY" : "EXIT", stage,
        read_register(reader, 0x37), read_register(reader, 0x14),
        last_irq[reader], last_error[reader], last_received[reader]);
}
/* Calculate the ISO14443A CRC used when selecting a card cascade level. */
static uint16_t card_crc(const uint8_t *data, int length)
{
    uint16_t crc = 0x6363;
    for (int index = 0; index < length; index++) {
        uint8_t value = data[index] ^ (uint8_t)crc;
        value ^= value << 4;
        crc = (crc >> 8) ^ ((uint16_t)value << 8) ^ ((uint16_t)value << 3) ^ (value >> 4);
    }
    return crc;
}
/* Exchange one bounded card command and reject errors, collisions and oversized replies. */
static int exchange(int reader, const uint8_t *send, int length, int bits, uint8_t *receive, int capacity)
{
    write_register(reader, 0x01, 0x00);
    write_register(reader, 0x04, 0x7f);
    write_register(reader, 0x0a, 0x80);
    write_fifo(reader, send, length);
    write_register(reader, 0x0d, bits);
    write_register(reader, 0x01, 0x0c);
    write_register(reader, 0x0d, bits | 0x80);
    int64_t deadline = now_ms() + 30;
    uint8_t flags = 0;
    while (now_ms() < deadline) {
        flags = read_register(reader, 0x04);
        if (flags & 0x31) break;
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    write_register(reader, 0x0d, bits);
    last_irq[reader] = flags;
    last_error[reader] = read_register(reader, 0x06);
    int received = read_register(reader, 0x0a);
    last_received[reader] = received;
    if (!(flags & 0x30) || (last_error[reader] & 0x1b)) return 0;
    if (received <= 0 || received > capacity || received > 16 || (read_register(reader, 0x0c) & 7)) return 0;
    read_fifo(reader, receive, received);
    return received;
}
/* Attach and configure both MFRC522 devices after the SD card enters SPI mode. */
void rfid_initialize(void)
{
    ESP_LOGI("RFID", "RFID build 3 (quiet idle): ENTRY CS=10, EXIT CS=9, CLK=12 MOSI=11 MISO=13 RST=15");
    gpio_reset_pin(RFID_RESET);
    gpio_set_direction(RFID_RESET, GPIO_MODE_OUTPUT);
    gpio_set_level(RFID_RESET, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(RFID_RESET, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
    int pins[] = { ENTRY_CS, EXIT_CS };
    for (int reader = 0; reader < 2; reader++) {
        spi_device_interface_config_t config = { .clock_speed_hz = 1000000, .mode = 0,
            .spics_io_num = pins[reader], .queue_size = 1 };
        ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &config, &readers[reader]));
        uint8_t version = read_register(reader, 0x37);
        reader_ready[reader] = version != 0 && version != 0xff;
        if (!reader_ready[reader]) {
            ESP_LOGE("RFID", "Reader %d not responding (version %02X)", reader, version);
            continue;
        }
        write_register(reader, 0x01, 0x0f);
        vTaskDelay(pdMS_TO_TICKS(50));
        write_register(reader, 0x2a, 0x8d);
        write_register(reader, 0x2b, 0x3e);
        write_register(reader, 0x2c, 0);
        write_register(reader, 0x2d, 30);
        write_register(reader, 0x15, 0x40);
        write_register(reader, 0x11, 0x3d);
        write_register(reader, 0x14, read_register(reader, 0x14) | 3);
        ESP_LOGI("RFID", "Reader %d ready, version %02X", reader, version);
    }
}
/* Wake and select one card, resolving cascade levels for 4, 7 and 10 byte UIDs. */
bool rfid_read(int reader, char *uid)
{
    if (!reader_ready[reader]) {
        report_reader_stage(reader, "Reader unavailable at startup");
        return false;
    }
    uint8_t receive[16];
    uint8_t wake = 0x52;
    if (exchange(reader, &wake, 1, 7, receive, sizeof(receive)) != 2) {
        /* An empty reader normally times out with no received bytes or hardware errors. */
        bool no_card = (last_irq[reader] & 0x01) && last_error[reader] == 0 && last_received[reader] == 0;
        if (!no_card) report_reader_stage(reader, "Invalid card wake response");
        return false;
    }
    uint8_t uid_bytes[10];
    int uid_length = 0;
    uint8_t levels[] = {0x93, 0x95, 0x97};
    for (int level = 0; level < 3; level++) {
        uint8_t command[9] = { levels[level], 0x20 };
        if (exchange(reader, command, 2, 0, receive, sizeof(receive)) != 5) {
            report_reader_stage(reader, "Card woke but UID response failed");
            return false;
        }
        if ((receive[0] ^ receive[1] ^ receive[2] ^ receive[3]) != receive[4]) {
            char message[96];
            snprintf(message, sizeof(message), "UID checksum failed: %02X %02X %02X %02X BCC=%02X expected=%02X",
                receive[0], receive[1], receive[2], receive[3], receive[4],
                receive[0] ^ receive[1] ^ receive[2] ^ receive[3]);
            report_reader_stage(reader, message);
            return false;
        }
        bool cascade = receive[0] == 0x88;
        int first = cascade ? 1 : 0;
        for (int index = first; index < 4; index++) {
            if (uid_length >= 10) return false;
            uid_bytes[uid_length++] = receive[index];
        }
        command[1] = 0x70;
        memcpy(command + 2, receive, 5);
        uint16_t crc = card_crc(command, 7);
        command[7] = crc & 255;
        command[8] = crc >> 8;
        if (exchange(reader, command, 9, 0, receive, sizeof(receive)) != 3) {
            report_reader_stage(reader, "UID received but card selection failed");
            return false;
        }
        crc = card_crc(receive, 1);
        if (receive[1] != (crc & 255) || receive[2] != (crc >> 8)) {
            report_reader_stage(reader, "Selection CRC failed");
            return false;
        }
        if (!(receive[0] & 4)) {
            if (cascade) return false;
            for (int index = 0; index < uid_length; index++) sprintf(uid + index * 2, "%02X", uid_bytes[index]);
            /* Halt the selected card so the next WUPA can detect a held card again. */
            uint8_t halt[] = {0x50, 0x00, 0x00, 0x00};
            crc = card_crc(halt, 2);
            halt[2] = crc & 255;
            halt[3] = crc >> 8;
            exchange(reader, halt, 4, 0, receive, sizeof(receive));
            return valid_uid(uid);
        }
        if (!cascade) return false;
    }
    return false;
}
