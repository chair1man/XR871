/*
 * Copyright (C) 2017 XRADIO TECHNOLOGY CO., LTD. All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *    1. Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *    2. Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the
 *       distribution.
 *    3. Neither the name of XRADIO TECHNOLOGY CO., LTD. nor the names of
 *       its contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <string.h>
#include <stdlib.h>
#include "lwip/tcpip.h"
#include "lwip/inet.h"
#include "net/wlan/wlan.h"
#include "net/udhcp/usr_dhcpd.h"

#include "common/framework/sys_ctrl/sys_ctrl.h"
#include "common/framework/sysinfo.h"
#include "net_ctrl.h"
#include "net_ctrl_debug.h"

struct netif_conf {
	uint8_t		bring_up;	// bring up or down
	uint8_t		use_dhcp;	// use DHCP or not
	ip_addr_t	ipaddr;
	ip_addr_t	netmask;
	ip_addr_t	gw;
};

#if LWIP_NETIF_LINK_CALLBACK
static void netif_link_callback(struct netif *netif)
{
	if (netif_is_link_up(netif)) {
		NET_DBG("netif is link up\n");
	} else {
		NET_DBG("netif is link down\n");
	}
}
#endif /* LWIP_NETIF_LINK_CALLBACK */

#if LWIP_NETIF_STATUS_CALLBACK
static void netif_status_callback(struct netif *netif)
{
	if (netif_is_up(netif)) {
		NET_DBG("netif is up\n");
		NET_DBG("address: %s\n", inet_ntoa(netif->ip_addr));
		NET_DBG("gateway: %s\n", inet_ntoa(netif->gw));
		NET_DBG("netmask: %s\n", inet_ntoa(netif->netmask));
		net_ctrl_msg_send(NET_CTRL_MSG_NETWORK_UP, 0);
	} else {
		NET_DBG("netif is down\n");
		net_ctrl_msg_send(NET_CTRL_MSG_NETWORK_DOWN, 0);
	}
}
#endif /* LWIP_NETIF_STATUS_CALLBACK */

#if LWIP_NETIF_REMOVE_CALLBACK
static void netif_remove_callback(struct netif *netif)
{
	NET_DBG("%s()\n", __func__);
}
#endif /* LWIP_NETIF_REMOVE_CALLBACK */

static void netif_config(struct netif *nif, struct netif_conf *conf)
{
	if (conf->bring_up) {
		if (netif_is_up(nif)) {
			NET_DBG("%s() netif is already up\n", __func__);
			return;
		}
		if (conf->use_dhcp) {
			if (nif->dhcp && (nif->dhcp->state != DHCP_OFF)) {
				NET_DBG("%s() DHCP is already started\n", __func__);
				return;
			}

			NET_DBG("%s() start DHCP...\n", __func__);
			if (netifapi_dhcp_start(nif) != ERR_OK) {
				NET_ERR("DHCP start failed!\n");
				return;
			}
		} else {
			netifapi_netif_set_addr(nif, &conf->ipaddr,
			                        &conf->netmask, &conf->gw);
			netifapi_netif_set_up(nif);
		}
	} else {
		if (conf->use_dhcp) {
			if (nif->dhcp == NULL) {
				NET_DBG("%s() DHCP is not started\n", __func__);
				return;
			}
			if (netif_is_link_up(nif)) {
				NET_DBG("%s() release DHCP\n", __func__);
				netifapi_netif_common(nif, NULL, dhcp_release);
			} else {
				NET_DBG("%s() bring down netif\n", __func__);
				netifapi_netif_set_down(nif);
				netifapi_netif_set_addr(nif, IP_ADDR_ANY, IP_ADDR_ANY,
				                        IP_ADDR_ANY);
			}

			NET_DBG("%s() stop DHCP\n", __func__);
			netifapi_dhcp_stop(nif);
		} else {
			if (!netif_is_up(nif)) {
				NET_DBG("%s() netif is already down\n", __func__);
				return;
			}
			NET_DBG("%s() bring down netif\n", __func__);
			netifapi_netif_set_down(nif);
			netifapi_netif_set_addr(nif, IP_ADDR_ANY, IP_ADDR_ANY,
			                        IP_ADDR_ANY);
		}
	}
}

/* bring up/down network */
void net_config(struct netif *nif, uint8_t bring_up)
{
	struct netif_conf net_conf;
	memset(&net_conf, 0, sizeof(struct netif_conf));
	net_conf.bring_up = bring_up;

	/* TODO: load network configuration */
	enum wlan_mode mode = wlan_if_get_mode(nif);
	struct sysinfo_netif_param netif_param;
	if (mode == WLAN_MODE_STA) {
		sysinfo_get(SYSINFO_STA_NETIF_PARAM, &netif_param);
	} else if (mode == WLAN_MODE_HOSTAP) {
		sysinfo_get(SYSINFO_AP_NETIF_PARAM, &netif_param);
	} else {
		NET_ERR("Invalid wlan mode %d\n", mode);
		return;
	}
	net_conf.use_dhcp = netif_param.use_dhcp;
	memcpy(&net_conf.ipaddr, &netif_param.ip_addr, sizeof(net_conf.ipaddr));
	memcpy(&net_conf.netmask, &netif_param.net_mask, sizeof(net_conf.netmask));
	memcpy(&net_conf.gw, &netif_param.gateway, sizeof(net_conf.gw));
	netif_config(nif, &net_conf);
}

struct netif *net_open(enum wlan_mode mode)
{
	struct netif *nif;

	NET_DBG("%s() ...\n", __func__);

	wlan_attach();
	nif = wlan_if_create(mode);
#if LWIP_NETIF_LINK_CALLBACK
	netif_set_link_callback(nif, netif_link_callback);
#endif
#if LWIP_NETIF_STATUS_CALLBACK
	netif_set_status_callback(nif, netif_status_callback);
#endif
#if LWIP_NETIF_REMOVE_CALLBACK
	netif_set_remove_callback(nif, netif_remove_callback);
#endif
	wlan_start(nif);
	sysinfo_set(SYSINFO_WLAN_MODE, &mode);

	return nif;
}

void net_close(struct netif *nif)
{
	NET_DBG("%s() ...\n", __func__);

	wlan_stop();
#if 0
#if LWIP_NETIF_REMOVE_CALLBACK
	netif_set_remove_callback(nif, NULL);
#endif
#if LWIP_NETIF_STATUS_CALLBACK
	netif_set_status_callback(nif, NULL);
#endif
#if LWIP_NETIF_LINK_CALLBACK
	netif_set_link_callback(nif, NULL);
#endif
#endif
	wlan_if_delete(nif);
	wlan_detach();
}

int net_ctrl_msg_send(uint16_t type, uint32_t data)
{
	return sys_event_send(CTRL_MSG_TYPE_NETWORK, type, data, OS_WAIT_FOREVER);
}

int net_ctrl_msg_send_with_free(uint16_t type, uint32_t data)
{
	return sys_event_send_with_free(CTRL_MSG_TYPE_NETWORK, type, data, OS_WAIT_FOREVER);
}

#if NET_DBG_ON
const char *net_ctrl_msg_str[] = {
	"wlan connected",
	"wlan disconnected",
	"wlan scan success",
	"wlan scan failed",
	"wlan 4way handshake failed",
	"wlan connect failed",
	"wlan smart config result",
	"wlan airkiss result",
	"network up",
	"network down",
};
#endif

static void net_ctrl_smart_config_result_handle(uint8_t *dest, uint8_t *src)
{
	int len = strlen((char *) src);

	int i = 0;
	char temp;
	for (i = 0; i < len; i++) {
		sprintf(&temp, "%x", ((src[i] & 0xf0) >> 4));
		dest[i * 2] = temp;
		sprintf(&temp, "%x", src[i] & 0x0f);
		dest[i * 2 + 1] = temp;
	}

	dest[len * 2] = 0;
}

static int net_ctrl_process_smart_config_result(struct wlan_smart_config_result *result)
{
	if (!result->valid) {
		NET_DBG("invalid smart config result\n");
		return 0;
	}

	NET_DBG("smart config ssid: %s\n", result->ssid);
	NET_DBG("smart config psk: %s\n", result->psk);

	int ssid_buf_size = strlen((char *)result->ssid) * 2 + 1;
	uint8_t ssid_buf[ssid_buf_size];
	memset(ssid_buf, 0, ssid_buf_size);

	net_ctrl_smart_config_result_handle(ssid_buf, result->ssid);

	if (strlen((char *)result->psk) != 0) {
		int psk_buf_size = strlen((char *)result->psk) + 2;
		uint8_t psk_buf[psk_buf_size];
		memset(psk_buf, 0, psk_buf_size);
		sprintf((char *)psk_buf, "\"%s\"", result->psk);

		if (wlan_sta_set(ssid_buf, psk_buf) != 0) {
			NET_WRN("set ssid failed\n");
			return -1;
		}
	} else {
		if (wlan_sta_set(ssid_buf, NULL) != 0) {
			NET_WRN("set ssid failed\n");
			return -1;
		}
	}

	if (wlan_sta_enable()!= 0) {
		NET_WRN("enable sta failed\n");
		return -1;
	}

	return wlan_sta_connect();
}

int net_ctrl_connect_ap(void)
{
	return 0;
}

int net_ctrl_disconnect_ap(void)
{
	return 0;
}

void net_ctrl_msg_process(uint32_t event, uint32_t data)
{
	uint16_t type = EVENT_SUBTYPE(event);
	NET_DBG("msg <%s>\n", net_ctrl_msg_str[type]);

	switch (type) {
	case NET_CTRL_MSG_WLAN_CONNECTED:
		if (g_wlan_netif) {
			/* set link up */
			tcpip_callback((tcpip_callback_fn)netif_set_link_up,
				       g_wlan_netif);
			/* bring up network */
			net_config(g_wlan_netif, 1);
		}
		break;
	case NET_CTRL_MSG_WLAN_DISCONNECTED:
		if (g_wlan_netif) {
			/* set link down */
			tcpip_callback((tcpip_callback_fn)netif_set_link_down,
				       g_wlan_netif);
		}
		/* if dhcp is started and not bound, stop it */
		if (g_wlan_netif && g_wlan_netif->dhcp &&
		    g_wlan_netif->dhcp->state != DHCP_OFF &&
		    g_wlan_netif->dhcp->state != DHCP_BOUND) {
			net_config(g_wlan_netif, 0);
		}
		break;
	case NET_CTRL_MSG_WLAN_SCAN_SUCCESS:
		break;
	case NET_CTRL_MSG_WLAN_SCAN_FAILED:
		break;
	case NET_CTRL_MSG_WLAN_4WAY_HANDSHAKE_FAILED:
		break;
	case NET_CTRL_MSG_WLAN_CONNECT_FAILED:
		break;
	case NET_CTRL_MSG_NETWORK_UP:
		if (g_wlan_netif) {
			enum wlan_mode mode = wlan_if_get_mode(g_wlan_netif);
			if (mode == WLAN_MODE_STA) {
				wlan_set_ip_addr(g_wlan_netif->state,
								 (uint8_t *)&g_wlan_netif->ip_addr.addr,
								 sizeof(g_wlan_netif->ip_addr.addr));
			} else if (mode == WLAN_MODE_HOSTAP) {
				dhcp_server_start(NULL);
			} else {
				NET_ERR("Invalid wlan mode %d\n", mode);
			}
		}
		break;
	case NET_CTRL_MSG_NETWORK_DOWN:
		break;
	case NET_CTRL_MSG_WLAN_SMART_CONFIG_RESULT:
		wlan_smart_config_stop();
		net_ctrl_process_smart_config_result((void *)data);
		break;
	case NET_CTRL_MSG_WLAN_AIRKISS_RESULT:
		wlan_airkiss_ack_start((void *)data, g_wlan_netif);
		net_ctrl_process_smart_config_result((void *)data);
		break;
	default:
		NET_DBG("unknown msg (%u, %u)\n", type, data);
		break;
	}
}

int net_ctrl_init(void)
{
	observer_base *ob = sys_callback_observer_create(CTRL_MSG_TYPE_NETWORK,
	                                                 NET_CTRL_MSG_ALL,
	                                                 net_ctrl_msg_process);
	if (ob == NULL)
		return -1;
	if (sys_ctrl_attach(ob) != 0)
		return -1;

	return 0;
}
