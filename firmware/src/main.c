/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <stdio.h>
#include <zephyr.h>

#include <dk_buttons_and_leds.h>
#include <modem/lte_lc.h>
#include <net/socket.h>

#include <memfault/core/data_packetizer.h>
#include <memfault/core/trace_event.h>
#include <memfault/metrics/metrics.h>
#include <memfault/ports/zephyr/http.h>

#include <logging/log.h>

LOG_MODULE_REGISTER(memfault_sample_udp, CONFIG_MEMFAULT_SAMPLE_UDP_LOG_LEVEL);

static K_SEM_DEFINE(lte_connected, 0, 1);

static void lte_handler(const struct lte_lc_evt *const evt) {
  switch (evt->type) {
  case LTE_LC_EVT_NW_REG_STATUS:
    if ((evt->nw_reg_status != LTE_LC_NW_REG_REGISTERED_HOME) &&
        (evt->nw_reg_status != LTE_LC_NW_REG_REGISTERED_ROAMING)) {
      break;
    }

    LOG_INF("Network registration status: %s",
            evt->nw_reg_status == LTE_LC_NW_REG_REGISTERED_HOME
                ? "Connected - home network"
                : "Connected - roaming");

    k_sem_give(&lte_connected);
    break;
  case LTE_LC_EVT_PSM_UPDATE:
    LOG_DBG("PSM parameter update: TAU: %d, Active time: %d", evt->psm_cfg.tau,
            evt->psm_cfg.active_time);
    break;
  case LTE_LC_EVT_EDRX_UPDATE: {
    char log_buf[60];
    ssize_t len;

    len = snprintf(log_buf, sizeof(log_buf),
                   "eDRX parameter update: eDRX: %f, PTW: %f",
                   evt->edrx_cfg.edrx, evt->edrx_cfg.ptw);
    if (len > 0) {
      LOG_DBG("%s", log_strdup(log_buf));
    }
    break;
  }
  case LTE_LC_EVT_RRC_UPDATE:
    LOG_DBG("RRC mode: %s",
            evt->rrc_mode == LTE_LC_RRC_MODE_CONNECTED ? "Connected" : "Idle");
    break;
  case LTE_LC_EVT_CELL_UPDATE:
    LOG_DBG("LTE cell changed: Cell ID: %d, Tracking area: %d", evt->cell.id,
            evt->cell.tac);
    break;
  case LTE_LC_EVT_LTE_MODE_UPDATE:
    LOG_INF("Active LTE mode changed: %s",
            evt->lte_mode == LTE_LC_LTE_MODE_NONE    ? "None"
            : evt->lte_mode == LTE_LC_LTE_MODE_LTEM  ? "LTE-M"
            : evt->lte_mode == LTE_LC_LTE_MODE_NBIOT ? "NB-IoT"
                                                     : "Unknown");
    break;
  default:
    break;
  }
}

static void modem_configure(void) {
#if defined(CONFIG_NRF_MODEM_LIB)
  if (IS_ENABLED(CONFIG_LTE_AUTO_INIT_AND_CONNECT)) {
    /* Do nothing, modem is already configured and LTE connected. */
  } else {
    int err;

    err = lte_lc_init_and_connect_async(lte_handler);
    if (err) {
      LOG_ERR("Modem could not be configured, error: %d", err);
      return;
    }

    /* Check LTE events of type LTE_LC_EVT_NW_REG_STATUS in
     * lte_handler() to determine when the LTE link is up.
     */
  }
#endif
}

/* Recursive Fibonacci calculation used to trigger stack overflow. */
static int fib(int n) {
  if (n <= 1) {
    return n;
  }

  return fib(n - 1) + fib(n - 2);
}

#define UDP_IP_HEADER_SIZE 28

static int client_fd;
static struct sockaddr_storage host_addr;
static struct k_delayed_work memfault_chunk_sender_work;

static void memfault_chunk_sender_work_fn(struct k_work *work) {
  uint8_t buffer[CONFIG_UDP_DATA_UPLOAD_SIZE_BYTES];

  bool data_available =
      memfault_packetizer_get_chunk(buffer, CONFIG_UDP_DATA_UPLOAD_SIZE_BYTES);
  if (data_available) {
    int err;

    printk("Transmitting UDP/IP payload of %d bytes to the ",
           CONFIG_UDP_DATA_UPLOAD_SIZE_BYTES + UDP_IP_HEADER_SIZE);
    printk("IP address %s, port number %d\n", CONFIG_UDP_SERVER_ADDRESS_STATIC,
           CONFIG_UDP_SERVER_PORT);

    err = send(client_fd, buffer, sizeof(buffer), 0);
    if (err < 0) {
      printk("Failed to transmit UDP packet, %d\n", errno);
    }
  }

  k_delayed_work_submit(&memfault_chunk_sender_work,
                        K_SECONDS(CONFIG_UDP_DATA_UPLOAD_FREQUENCY_SECONDS));
}

static void init_memfault_chunks_sender(void) {
  k_delayed_work_init(&memfault_chunk_sender_work,
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
    goto error;
  }

  err = connect(client_fd, (struct sockaddr *)&host_addr,
                sizeof(struct sockaddr_in));
  if (err < 0) {
    printk("Connect failed : %d\n", errno);
    goto error;
  }

  return 0;

error:
  server_disconnect();

  return err;
}

/* Handle button presses and trigger faults that can be captured and sent to
 * the Memfault cloud for inspection after rebooting:
 * Only button 1 is available on Thingy:91, the rest are available on nRF9160
 *DK. Button 1: Trigger stack overflow. Button 2: Trigger NULL-pointer
 *dereference. Switch 1: Increment switch_1_toggle_count metric by one. Switch
 *2: Trace switch_2_toggled event, along with switch state.
 */
static void button_handler(uint32_t button_states, uint32_t has_changed) {
  uint32_t buttons_pressed = has_changed & button_states;

  if (buttons_pressed & DK_BTN1_MSK) {
    LOG_WRN("Stack overflow will now be triggered");
    fib(10000);
  } else if (buttons_pressed & DK_BTN2_MSK) {
    uint32_t *ptr = NULL;
    volatile uint32_t i = *ptr;

    LOG_WRN("NULL pointer de-reference will now be triggered");

    (void)i;
  } else if (has_changed & DK_BTN3_MSK) {
    /* DK_BTN3_MSK is Switch 1 on nRF9160 DK. */
    int err = memfault_metrics_heartbeat_add(
        MEMFAULT_METRICS_KEY(switch_1_toggle_count), 1);

    if (err) {
      LOG_ERR("Failed to increment switch_1_toggle_count");
    } else {
      LOG_INF("switch_1_toggle_count incremented");
    }
  } else if (has_changed & DK_BTN4_MSK) {
    /* DK_BTN4_MSK is Switch 2 on nRF9160 DK. */
    MEMFAULT_TRACE_EVENT_WITH_LOG(switch_2_toggled, "Switch state: %d",
                                  buttons_pressed & DK_BTN4_MSK ? 1 : 0);
  }
}

void main(void) {
  int err;
  uint32_t time_to_lte_connection;

  printk("Memfault over UDP sample has started\n");

  modem_configure();

  err = dk_buttons_init(button_handler);
  if (err) {
    LOG_ERR("dk_buttons_init, error: %d", err);
  }

  LOG_INF("Connecting to LTE network, this may take several minutes...");

  k_sem_take(&lte_connected, K_FOREVER);

  /* Retrieve the LTE time to connect metric. */
  // These are crashing the device for now
  // memfault_metrics_heartbeat_timer_read(
  //    MEMFAULT_METRICS_KEY(lte_time_to_connect), &time_to_lte_connection);

  LOG_INF("Connected to LTE network. Time to connect: %d ms",
          time_to_lte_connection);
  LOG_INF("Sending already captured data to Memfault");

  /* Trigger collection of heartbeat data. */
  memfault_metrics_heartbeat_debug_trigger();

  err = server_init();
  if (err) {
    printk("Not able to initialize UDP server connection\n");
    return;
  }

  err = server_connect();
  if (err) {
    printk("Not able to connect to UDP server\n");
    return;
  }

  init_memfault_chunks_sender();

  k_delayed_work_submit(&memfault_chunk_sender_work, K_NO_WAIT);
}

