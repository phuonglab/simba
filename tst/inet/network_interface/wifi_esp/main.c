/**
 * @file main.c
 *
 * @section License
 * Copyright (C) 2014-2016, Erik Moqvist
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * This file is part of the Simba project.
 */

#include "simba.h"

#if !defined(SSID)
#    pragma message "WiFi connection variable SSID is not set. Using default value MySSID"
#    define SSID ssid
#endif

#if !defined(PASSWORD)
#    pragma message "WiFi connection variable PASSWORD is not set. Using default value MyPassword"
#    define PASSWORD password
#endif

#if !defined(ESP8266_IP)
#    pragma message "WiFi connection variable ESP8266_IP is not set. Using default value 192.168.0.5"
#    define ESP8266_IP 192.168.0.5
#endif

/* Ports. */
#define UDP_PORT               30303
#define TCP_PORT               40404
#define TCP_PORT_WRITE_CLOSE   40405
#define TCP_PORT_SIZES         40406

static struct network_interface_wifi_t wifi_sta;
static struct network_interface_wifi_t wifi_ap;
static uint8_t buffer[5000];

static int test_init(struct harness_t *harness_p)
{
    struct inet_if_ip_info_t info;
    char buf[20];

    BTASSERT(esp_wifi_module_init() == 0);
    BTASSERT(network_interface_module_init() == 0);
    BTASSERT(socket_module_init() == 0);

    /* Initialize a WiFi in station mode with given SSID and
       password. */
    BTASSERT(network_interface_wifi_init(&wifi_sta,
                                         "esp-wlan-sta",
                                         &network_interface_wifi_driver_esp_station,
                                         NULL,
                                         STRINGIFY(SSID),
                                         STRINGIFY(PASSWORD)) == 0);
    BTASSERT(network_interface_add(&wifi_sta.network_interface) == 0);
    BTASSERT(network_interface_start(&wifi_sta.network_interface) == 0);

    /* Setup a SoftAP as well. */
    BTASSERT(network_interface_wifi_init(&wifi_ap,
                                         "esp-wlan-ap",
                                         &network_interface_wifi_driver_esp_softap,
                                         NULL,
                                         "Simba",
                                         "password") == 0);
    BTASSERT(network_interface_add(&wifi_ap.network_interface) == 0);
    BTASSERT(network_interface_start(&wifi_ap.network_interface) == 0);

    std_printf(FSTR("Connecting to SSID=%s\r\n"), STRINGIFY(SSID));

    /* Wait for a connection to the WiFi access point. */
    while (network_interface_is_up(&wifi_sta.network_interface) == 0) {
        thrd_sleep(1);
    }

    BTASSERT(network_interface_get_ip_info(&wifi_sta.network_interface,
                                           &info) == 0);

    std_printf(FSTR("Connected to Access Point (AP). Got IP %s.\r\n"),
               inet_ntoa(&info.address, buf));

    BTASSERT(network_interface_get_by_name("esp-wlan-sta")
             == &wifi_sta.network_interface);
    BTASSERT(network_interface_get_by_name("esp-wlan-ap")
             == &wifi_ap.network_interface);
    BTASSERT(network_interface_get_by_name("none") == NULL);
    
    return (0);
}

static int test_udp(struct harness_t *harness_p)
{
    struct socket_t sock;
    struct inet_addr_t addr;
    char buf[16];
    char addrbuf[20];
    size_t size;
    struct chan_list_t list;
    int workspace[16];

    BTASSERT(chan_list_init(&list, workspace, sizeof(workspace)) == 0);

    std_printf(FSTR("UDP test\r\n"));

    std_printf(FSTR("opening socket\r\n"));
    BTASSERT(socket_open_udp(&sock) == 0);

    BTASSERT(chan_list_add(&list, &sock) == 0);

    std_printf(FSTR("binding to %d\r\n"), UDP_PORT);
    inet_aton(STRINGIFY(ESP8266_IP), &addr.ip);
    addr.port = UDP_PORT;
    BTASSERT(socket_bind(&sock, &addr) == 0);

    std_printf(FSTR("recvfrom\r\n"));

    size = socket_recvfrom(&sock, buf, sizeof(buf), 0, &addr);
    BTASSERT(size == 9);
    buf[size] = '\0';
    std_printf(FSTR("received '%s' from %s:%d\r\n"),
               buf,
               inet_ntoa(&addr.ip, addrbuf),
               addr.port);

    std_printf(FSTR("sending '%s' to %s:%d\r\n"),
               buf,
               inet_ntoa(&addr.ip, addrbuf),
               addr.port);
    BTASSERT(socket_sendto(&sock, buf, size, 0, &addr) == size);

    BTASSERT(chan_list_poll(&list, NULL) == &sock);

    size = socket_recvfrom(&sock, buf, sizeof(buf), 0, &addr);
    BTASSERT(size == 9);
    buf[size] = '\0';
    std_printf(FSTR("received '%s' from %s:%d\r\n"),
               buf,
               inet_ntoa(&addr.ip, addrbuf),
               addr.port);

    std_printf(FSTR("sending '%s' to %s:%d\r\n"),
               buf,
               inet_ntoa(&addr.ip, addrbuf),
               addr.port);
    BTASSERT(socket_sendto(&sock, buf, size, 0, &addr) == size);

    std_printf(FSTR("closing socket\r\n"));
    BTASSERT(socket_close(&sock) == 0);

    return (0);
}

static int test_tcp(struct harness_t *harness_p)
{
    struct socket_t listener;
    struct socket_t client;
    struct inet_addr_t addr;
    char buf[16];
    char addrbuf[20];
    size_t size;
    struct chan_list_t list;
    int workspace[16];

    BTASSERT(chan_list_init(&list, workspace, sizeof(workspace)) == 0);

    std_printf(FSTR("TCP test\r\n"));

    std_printf(FSTR("opening listener socket\r\n"));
    BTASSERT(socket_open_tcp(&listener) == 0);

    std_printf(FSTR("binding to %d\r\n"), TCP_PORT);
    inet_aton(STRINGIFY(ESP8266_IP), &addr.ip);
    addr.port = TCP_PORT;
    BTASSERT(socket_bind(&listener, &addr) == 0);

    BTASSERT(socket_listen(&listener, 5) == 0);

    std_printf(FSTR("listening on %d\r\n"), TCP_PORT);

    BTASSERT(socket_accept(&listener, &client, &addr) == 0);
    std_printf(FSTR("accepted client %s:%d\r\n"),
               inet_ntoa(&addr.ip, addrbuf),
               addr.port);

    BTASSERT(chan_list_add(&list, &client) == 0);

    size = socket_read(&client, buf, 5);
    BTASSERT(size == 5);
    size += socket_read(&client, &buf[5], 4);
    BTASSERT(size == 9);
    buf[size] = '\0';
    std_printf(FSTR("read %d bytes: '%s'\r\n"), size, buf);

    std_printf(FSTR("writing '%s'\r\n"), buf);
    BTASSERT(socket_write(&client, buf, size) == size);

    BTASSERT(chan_list_poll(&list, NULL) == &client);

    size = socket_read(&client, buf, 5);
    BTASSERT(size == 5);
    size += socket_read(&client, &buf[5], 4);
    BTASSERT(size == 9);
    buf[size] = '\0';
    std_printf(FSTR("read %d bytes: '%s'\r\n"), size, buf);

    std_printf(FSTR("writing '%s'\r\n"), buf);
    BTASSERT(socket_write(&client, buf, size) == size);

    std_printf(FSTR("closing client socket\r\n"));
    BTASSERT(socket_close(&client) == 0);

    std_printf(FSTR("closing listener socket\r\n"));
    BTASSERT(socket_close(&listener) == 0);

    return (0);
}

static int test_tcp_write_close(struct harness_t *harness_p)
{
    struct socket_t listener;
    struct socket_t client;
    struct inet_addr_t addr;
    char addrbuf[20];

    std_printf(FSTR("TCP test\r\n"));

    std_printf(FSTR("opening listener socket\r\n"));
    BTASSERT(socket_open_tcp(&listener) == 0);

    std_printf(FSTR("binding to %d\r\n"), TCP_PORT_WRITE_CLOSE);
    inet_aton(STRINGIFY(ESP8266_IP), &addr.ip);
    addr.port = TCP_PORT_WRITE_CLOSE;
    BTASSERT(socket_bind(&listener, &addr) == 0);

    BTASSERT(socket_listen(&listener, 5) == 0);

    std_printf(FSTR("listening on %d\r\n"), TCP_PORT_WRITE_CLOSE);

    BTASSERT(socket_accept(&listener, &client, &addr) == 0);
    std_printf(FSTR("accepted client %s:%d\r\n"),
               inet_ntoa(&addr.ip, addrbuf),
               addr.port);

    thrd_sleep(1);

    BTASSERT(socket_read(&client, &buffer[0], 533) == 533);

    /* The socket is closed by the peer. */
    std_printf(FSTR("reading from closed socket\r\n"));
    BTASSERT(socket_read(&client, &buffer[533], 1) == 0);

    std_printf(FSTR("closing client socket\r\n"));
    BTASSERT(socket_close(&client) == 0);

    std_printf(FSTR("closing listener socket\r\n"));
    BTASSERT(socket_close(&listener) == 0);

    return (0);
}

static int test_tcp_sizes(struct harness_t *harness_p)
{
    struct socket_t listener;
    struct socket_t client;
    struct inet_addr_t addr;
    char addrbuf[20];
    ssize_t size;
    size_t offset;
    struct chan_list_t list;
    int workspace[16];

    std_printf(FSTR("TCP test\r\n"));

    BTASSERT(chan_list_init(&list, workspace, sizeof(workspace)) == 0);

    std_printf(FSTR("opening listener socket\r\n"));
    BTASSERT(socket_open_tcp(&listener) == 0);

    BTASSERT(chan_list_add(&list, &listener) == 0);

    std_printf(FSTR("binding to %d\r\n"), TCP_PORT_SIZES);
    inet_aton(STRINGIFY(ESP8266_IP), &addr.ip);
    addr.port = TCP_PORT_SIZES;
    BTASSERT(socket_bind(&listener, &addr) == 0);

    BTASSERT(socket_listen(&listener, 5) == 0);

    std_printf(FSTR("listening on %d\r\n"), TCP_PORT_SIZES);

    BTASSERT(chan_list_poll(&list, NULL) == &listener);

    BTASSERT(socket_accept(&listener, &client, &addr) == 0);
    std_printf(FSTR("accepted client %s:%d\r\n"),
               inet_ntoa(&addr.ip, addrbuf),
               addr.port);

    /* Range of packet sizes. */
    for (size = 1; size < sizeof(buffer); size += 128) {
        BTASSERT(socket_read(&client, buffer, size) == size);
        BTASSERT(socket_write(&client, buffer, size) == size);
    }

    /* Send a 1800 bytes packet and recieve small chunks of it. */
    size = 0;
    offset = 0;

    while (offset < 1800) {
        size = (1800 - offset);

        if (size > 128) {
            size = 128;
        }

        size = socket_read(&client, &buffer[offset], size);
        std_printf(FSTR("read %d bytes at offset %d\r\n"), size, offset);
        BTASSERT((size > 0) && (size <= 128));
        offset += size;
    }

    BTASSERT(socket_write(&client, buffer, offset) == offset);

    std_printf(FSTR("closing client socket\r\n"));
    BTASSERT(socket_close(&client) == 0);

    std_printf(FSTR("closing listener socket\r\n"));
    BTASSERT(socket_close(&listener) == 0);

    return (0);
}

static int test_print(struct harness_t *harness_p)
{
    char command[64];

    strcpy(command, "filesystems/fs/counters/list");
    BTASSERT(fs_call(command, NULL, sys_get_stdout(), NULL) == 0);

    strcpy(command, "drivers/esp_wifi/status");
    BTASSERT(fs_call(command, NULL, sys_get_stdout(), NULL) == 0);

    strcpy(command, "inet/network_interface/list");
    BTASSERT(fs_call(command, NULL, sys_get_stdout(), NULL) == 0);

    return (0);
}

int main()
{
    struct harness_t harness;
    struct harness_testcase_t harness_testcases[] = {
        { test_init, "test_init" },
        { test_udp, "test_udp" },
        { test_tcp, "test_tcp" },
        { test_tcp_write_close, "test_tcp_write_close" },
        { test_tcp_sizes, "test_tcp_sizes" },
        { test_print, "test_print" },
        { NULL, NULL }
    };

    sys_start();
    inet_module_init();

    harness_init(&harness);
    harness_run(&harness, harness_testcases);

    return (0);
}