/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <stdio.h>
#include <zephyr.h>

#include <dk_buttons_and_leds.h>
#include <net/socket.h>

#include "memfault/core/platform/device_info.h"
#include <memfault/core/data_packetizer.h>
#include <memfault/core/trace_event.h>
#include <memfault/metrics/metrics.h>
#include <memfault/ports/zephyr/http.h>

#include <logging/log.h>

LOG_MODULE_REGISTER(memfault_sample, CONFIG_MEMFAULT_SAMPLE_LOG_LEVEL);

static int client_fd;
static struct sockaddr_storage host_addr;
static struct k_work_delayable memfault_chunk_sender_work;

static uint8_t udp_message[CONFIG_UDP_DATA_UPLOAD_SIZE_BYTES];

typedef struct UdpMessageChunkSection {
  uint8_t *start_addr;
  size_t size;
} sUdpMessageChunkSection;

static sUdpMessageChunkSection udp_message_chunk_section;

#define NUMBER_OF_SECTIONS 3
static void init_udp_message(void) {
  sMemfaultDeviceInfo device_info;
  memfault_platform_get_device_info(&device_info);

  size_t remaining_bytes = CONFIG_UDP_DATA_UPLOAD_SIZE_BYTES;
  uint8_t *cursor = udp_message;

  size_t section_size = sizeof(CONFIG_UDP_DATA_UPLOAD_VERSION_PREFIX);
  strncpy(cursor, CONFIG_UDP_DATA_UPLOAD_VERSION_PREFIX, remaining_bytes);
  remaining_bytes -= section_size;
  cursor += section_size;

  section_size = sizeof(CONFIG_MEMFAULT_NCS_PROJECT_KEY);
  strncpy(cursor, CONFIG_MEMFAULT_NCS_PROJECT_KEY, remaining_bytes);
  remaining_bytes -= section_size;
  cursor += section_size;

  section_size = strlen((char *)device_info.device_serial) + 1;
  strncpy(cursor, device_info.device_serial, remaining_bytes);
  remaining_bytes -= section_size;
  cursor += section_size;

  LOG_DBG("Successfully initialized udp message buffer");

  udp_message_chunk_section = (sUdpMessageChunkSection){
      .size = remaining_bytes,
      .start_addr = (uint8_t *)cursor,
  };
}

#define UDP_IP_HEADER_SIZE 28

static void memfault_chunk_sender_work_fn(struct k_work *work) {
  memfault_metrics_heartbeat_debug_print();

  size_t chunk_buffer_len = udp_message_chunk_section.size;
  size_t size_of_prelude = CONFIG_UDP_DATA_UPLOAD_SIZE_BYTES - chunk_buffer_len;
  const bool success = memfault_packetizer_get_chunk(
      udp_message_chunk_section.start_addr, &chunk_buffer_len);

  // under-documented edge-case: success but no data
  if (success && (chunk_buffer_len > 0)) {
    int err;
    size_t udp_message_size = size_of_prelude + chunk_buffer_len;

    printk("Transmitting UDP/IP payload of %d bytes to the ",
           udp_message_size + UDP_IP_HEADER_SIZE);
    printk("IP address %s, port number %d\n", CONFIG_UDP_SERVER_ADDRESS_STATIC,
           CONFIG_UDP_SERVER_PORT);

    err = send(client_fd, udp_message, udp_message_size, 0);
    if (err < 0) {
      printk("Failed to transmit UDP packet, %d\n", errno);
    }
  } else {
    printk("No Memfault chunks to upload!\n");
  }

  k_work_schedule(&memfault_chunk_sender_work,
                  K_SECONDS(CONFIG_UDP_DATA_UPLOAD_FREQUENCY_SECONDS));
}

static void init_memfault_chunks_sender(void) {
  k_work_init_delayable(&memfault_chunk_sender_work,
                        memfault_chunk_sender_work_fn);
}

static void server_disconnect(void) { (void)close(client_fd); }

static int server_init(void) {
  struct sockaddr_in *server4 = ((struct sockaddr_in *)&host_addr);

  server4->sin_family = AF_INET;
  server4->sin_port = htons(CONFIG_UDP_SERVER_PORT);

  inet_pton(AF_INET, CONFIG_UDP_SERVER_ADDRESS_STATIC, &server4->sin_addr);

  return 0;
}

static int server_connect(void) {
  int err;

  client_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (client_fd < 0) {
    printk("Failed to create UDP socket: %d\n", errno);
    server_disconnect();
    return err;
  }

  err = connect(client_fd, (struct sockaddr *)&host_addr,
                sizeof(struct sockaddr_in));
  if (err < 0) {
    printk("Failed to connect: %d\n", errno);
    server_disconnect();
    return err;
  }

  return 0;
}

void main(void) {
  int err;

  printk("Memfault over UDP sample has started\n");

  err = server_init();
  if (err) {
    printk("Failed to initialize UDP server connection\n");
    return;
  }

  err = server_connect();
  if (err) {
    printk("Failed to connect to UDP server\n");
    return;
  }

  init_udp_message();
  init_memfault_chunks_sender();
  k_work_schedule(&memfault_chunk_sender_work, K_NO_WAIT);
}
