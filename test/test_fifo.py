#!/usr/bin/env python3
"""Check actual FIFO helpers against the MFRC522 SPI framing contract."""
from pathlib import Path
import subprocess
root = Path(__file__).resolve().parents[1]
source = (root/'files_to_work_in/entrance/rfid.c').read_text()
helpers = []
for name in ['write_fifo','read_fifo']:
    start = source.index('static void '+name+'(')
    end = source.index('\n}\n',start)+3
    helpers.append(source[start:end])
program = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define configASSERT assert
#define ESP_ERROR_CHECK(result) assert((result) == 0)
typedef struct { unsigned length; const void *tx_buffer; void *rx_buffer; } spi_transaction_t;
static int readers[] = {10, 9};
static int expected_reader;
static int transfer_count;
static int response_length;
static uint8_t payload[16];
/* Model the SPI wire contract, including address repeats and final dummy byte. */
static int spi_device_transmit(int reader, spi_transaction_t *transaction)
{
    assert(reader == expected_reader);
    transfer_count++;
    assert(transaction->length == (unsigned)(response_length + 1) * 8);
    const uint8_t *tx = transaction->tx_buffer;
    if (transaction->rx_buffer) {
        for (int i = 0; i < response_length; i++) assert(tx[i] == 0x92);
        assert(tx[response_length] == 0);
        uint8_t *rx = transaction->rx_buffer;
        rx[0] = 0xee;
        memcpy(rx + 1, payload, response_length);
    } else {
        assert(tx[0] == 0x12);
        assert(memcmp(tx + 1, payload, response_length) == 0);
    }
    return 0;
}
'''
program += '\n'.join(helpers)
program += r'''
/* Check ATQA, SAK, UID/BCC and maximum-size transfers on both reader handles. */
int main(void)
{
    int lengths[] = {2, 3, 5, 16};
    for (int reader = 0; reader < 2; reader++) {
        expected_reader = readers[reader];
        for (int n = 0; n < 4; n++) {
            response_length = lengths[n];
            for (int i = 0; i < response_length; i++) payload[i] = i + 1;
            if (response_length == 5) payload[4] = 1 ^ 2 ^ 3 ^ 4;
            uint8_t received[16] = {0};
            transfer_count = 0;
            read_fifo(reader, received, response_length);
            assert(transfer_count == 1);
            assert(memcmp(received, payload, response_length) == 0);
            transfer_count = 0;
            write_fifo(reader, payload, response_length);
            assert(transfer_count == 1);
        }
    }
    puts("PASS: actual FIFO helpers use one correctly framed SPI transaction for each response/command on both readers.");
}
'''
work = root/'work/fifo-test'
work.mkdir(parents=True, exist_ok=True)
(work/'test_fifo.c').write_text(program)
subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',str(work/'test_fifo.c'),'-o',str(work/'test_fifo')],check=True)
subprocess.run([str(work/'test_fifo')],check=True)
