/*
 * Copyright (C) 2015-2017 Alibaba Group Holding Limited
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "iot_import.h"
#include "iot_export.h"
#include "iot_export_mqtt.h"
#include "aos/log.h"
#include "aos/yloop.h"
#include "aos/network.h"
#include <netmgr.h>
#include <aos/kernel.h>
#include <k_err.h>
#include <netmgr.h>
#include <aos/cli.h>
#include <aos/cloud.h>

#include "hal/hal.h"

#ifdef AOS_ATCMD
#include <atparser.h>
#endif

#if 0
#define PRODUCT_KEY             "a1E31Zmhcxo"
#define DEVICE_NAME             "QSUvUO7V5lxwJsOHgyHc"
#define DEVICE_SECRET           "O6iyf0lnZXJQEyHdyMGPASkEamb5cDEi"

#else

#define PRODUCT_KEY "b1dl9ooTlYa"
#define DEVICE_NAME "Ld_developerkit_test01_sh"
#define DEVICE_SECRET  "070aAXwERZaaPAfXXpmNkBF9VvslAC4q"

#endif

#define ALINK_BODY_FORMAT         "{\"id\":\"%d\",\"version\":\"1.0\",\"method\":\"%s\",\"params\":%s}"
#define ALINK_TOPIC_PROP_POST     "/sys/"PRODUCT_KEY"/"DEVICE_NAME"/thing/event/property/post"
#define ALINK_TOPIC_PROP_POSTRSP  "/sys/"PRODUCT_KEY"/"DEVICE_NAME"/thing/event/property/post_reply"
#define ALINK_TOPIC_PROP_SET      "/sys/"PRODUCT_KEY"/"DEVICE_NAME"/thing/service/property/set"
#define ALINK_METHOD_PROP_POST    "thing.event.property.post"

#define MSG_LEN_MAX             (1024)
/*
* Please check below item which in feature self-definition from "https://linkdevelop.aliyun.com/"
*/
#define PROP_SET_FORMAT_CMDLED       "\"cmd_led\":"

int cnt = 0;
static int is_subscribed = 0;
void *gpclient;
char msg_pub[512];
iotx_mqtt_topic_info_t msg;
char *msg_buf = NULL, *msg_readbuf = NULL;
int mqtt_setup(void);

/*
 * MQTT Subscribe handler
 * topic: ALINK_TOPIC_PROP_SET
 */
static void handle_prop_set(void *pcontext, void *pclient, iotx_mqtt_event_msg_pt msg)
{
#ifdef CLD_CMD_LED_REMOTE_CTRL_SUPPORT
    iotx_mqtt_topic_info_pt ptopic_info = (iotx_mqtt_topic_info_pt)msg->msg;
    char *p_serch = NULL;
    uint8_t led_cmd = 0;
    bool gpio_level = 0;
    p_serch = strstr(ptopic_info->payload, PROP_SET_FORMAT_CMDLED);
    if (p_serch != NULL) {
      led_cmd = *(p_serch + strlen(PROP_SET_FORMAT_CMDLED));
    } else {
      LOG("Failed to search, wrong topic!");
	}
    LOG("----");
    LOG("Topic: '%.*s' (Length: %d)", ptopic_info->topic_len,
                ptopic_info->ptopic, ptopic_info->topic_len);
    LOG("Payload: '%.*s' (Length: %d)", ptopic_info->payload_len,
               ptopic_info->payload, ptopic_info->payload_len);
    LOG("----");

    if (led_cmd == '1' || led_cmd == '0')
      gpio_level = led_cmd - '0';
    board_drv_led_ctrl(gpio_level);
#endif
}


/*
* MQTT Subscribe handler
* topic: ALINK_TOPIC_PROP_POSTRSP
*/
static void handle_prop_postrsp(void *pcontext, void *pclient,
                                iotx_mqtt_event_msg_pt msg)
{
    iotx_mqtt_topic_info_pt ptopic_info = (iotx_mqtt_topic_info_pt)msg->msg;

#if 0
    // print topic name and topic message
    LOG("----");
    LOG("Topic: '%.*s' (Length: %d)", ptopic_info->topic_len,
        ptopic_info->ptopic, ptopic_info->topic_len);
    LOG("Payload: '%.*s' (Length: %d)", ptopic_info->payload_len,
        ptopic_info->payload, ptopic_info->payload_len);
    LOG("----");
#endif
}

static void wifi_service_event(input_event_t *event, void *priv_data)
{
    if (event->type != EV_WIFI) {
        return;
    }

    if (event->code != CODE_WIFI_ON_GOT_IP) {
        return;
    }
    LOG("wifi_service_event!");
    mqtt_setup();
}

/*
 * MQTT subscribe, from fixed topic, alink protocol format
 */
static void mqtt_subscribe(void *pclient)
{
    int rc = -1;
    /* Subscribe the specific topic */
    rc = IOT_MQTT_Subscribe(pclient, ALINK_TOPIC_PROP_SET, IOTX_MQTT_QOS0,
                            handle_prop_set, NULL);
    if (rc < 0) {
        LOG("subscribe ALINK_TOPIC_PROP_SET failed, rc = %d", rc);
    }
    rc = IOT_MQTT_Subscribe(pclient, ALINK_TOPIC_PROP_POSTRSP, IOTX_MQTT_QOS0,
                            handle_prop_postrsp, NULL);
    if (rc < 0) {
        LOG("subscribe ALINK_TOPIC_PROP_POSTRSP failed, rc = %d", rc);
    }
}

/*
 * MQTT ready event handler
 */
static void mqtt_service_event(input_event_t *event, void *priv_data)
{
    char *pclient = priv_data;
    if (event->type == EV_SYS && event->code == CODE_SYS_ON_MQTT_READ) {
        LOG("mqtt service");
        mqtt_subscribe(pclient);
    } else {
        LOG("skip mqtt service");
    }
}

void event_handle_mqtt(void *pcontext, void *pclient, iotx_mqtt_event_msg_pt msg)
{
    uintptr_t packet_id = (uintptr_t)msg->msg;
    iotx_mqtt_topic_info_pt topic_info = (iotx_mqtt_topic_info_pt)msg->msg;

    switch (msg->event_type) {
        case IOTX_MQTT_EVENT_UNDEF:
            LOG("undefined event occur.");
            break;

        case IOTX_MQTT_EVENT_DISCONNECT:
            LOG("MQTT disconnect.");
            break;

        case IOTX_MQTT_EVENT_RECONNECT:
            LOG("MQTT reconnect.");
            break;

        case IOTX_MQTT_EVENT_SUBCRIBE_SUCCESS:
            LOG("subscribe success, packet-id=%u", (unsigned int)packet_id);
            break;

        case IOTX_MQTT_EVENT_SUBCRIBE_TIMEOUT:
            LOG("subscribe wait ack timeout, packet-id=%u", (unsigned int)packet_id);
            break;

        case IOTX_MQTT_EVENT_SUBCRIBE_NACK:
            LOG("subscribe nack, packet-id=%u", (unsigned int)packet_id);
            break;

        case IOTX_MQTT_EVENT_UNSUBCRIBE_SUCCESS:
            LOG("unsubscribe success, packet-id=%u", (unsigned int)packet_id);
            break;

        case IOTX_MQTT_EVENT_UNSUBCRIBE_TIMEOUT:
            LOG("unsubscribe timeout, packet-id=%u", (unsigned int)packet_id);
            break;

        case IOTX_MQTT_EVENT_UNSUBCRIBE_NACK:
            LOG("unsubscribe nack, packet-id=%u", (unsigned int)packet_id);
            break;

        case IOTX_MQTT_EVENT_PUBLISH_SUCCESS:
            LOG("publish success, packet-id=%u", (unsigned int)packet_id);
            break;

        case IOTX_MQTT_EVENT_PUBLISH_TIMEOUT:
            LOG("publish timeout, packet-id=%u", (unsigned int)packet_id);
            break;

        case IOTX_MQTT_EVENT_PUBLISH_NACK:
            LOG("publish nack, packet-id=%u", (unsigned int)packet_id);
            break;

        case IOTX_MQTT_EVENT_PUBLISH_RECVEIVED:
            LOG("topic message arrived but without any related handle: topic=%.*s, topic_msg=%.*s",
                topic_info->topic_len,
                topic_info->ptopic,
                topic_info->payload_len,
                topic_info->payload);
            break;

        default:
            LOG("Should NOT arrive here.");
            break;
    }
}

void release_buff()
{
    if (NULL != msg_buf) {
        aos_free(msg_buf);
    }

    if (NULL != msg_readbuf) {
        aos_free(msg_readbuf);
    }
}

int mqtt_setup(void)
{
    int rc = 0;
    iotx_conn_info_pt pconn_info;
    iotx_mqtt_param_t mqtt_params;

    if (msg_buf != NULL) {
        return rc;
    }

    if (NULL == (msg_buf = (char *)aos_malloc(MSG_LEN_MAX))) {
        LOG("not enough memory");
        rc = -1;
        release_buff();
        return rc;
    }

    if (NULL == (msg_readbuf = (char *)aos_malloc(MSG_LEN_MAX))) {
        LOG("not enough memory");
        rc = -1;
        release_buff();
        return rc;
    }

    /* Device AUTH */
    if (0 != IOT_SetupConnInfo(PRODUCT_KEY, DEVICE_NAME, DEVICE_SECRET, (void **)&pconn_info)) {
        LOG("AUTH request failed!");
        rc = -1;
        release_buff();
        return rc;
    }

    /* Initialize MQTT parameter */
    memset(&mqtt_params, 0x0, sizeof(mqtt_params));
    mqtt_params.port = pconn_info->port;
    mqtt_params.host = pconn_info->host_name;
    mqtt_params.client_id = pconn_info->client_id;
    mqtt_params.username = pconn_info->username;
    mqtt_params.password = pconn_info->password;
    mqtt_params.pub_key = pconn_info->pub_key;
    mqtt_params.request_timeout_ms = 2000;
    mqtt_params.clean_session = 0;
    mqtt_params.keepalive_interval_ms = 60000;
    mqtt_params.pread_buf = msg_readbuf;
    mqtt_params.read_buf_size = MSG_LEN_MAX;
    mqtt_params.pwrite_buf = msg_buf;
    mqtt_params.write_buf_size = MSG_LEN_MAX;
    mqtt_params.handle_event.h_fp = event_handle_mqtt;
    mqtt_params.handle_event.pcontext = NULL;

    /* Construct a MQTT client with specify parameter */
    gpclient = IOT_MQTT_Construct(&mqtt_params);
    if (NULL == gpclient) {
        LOG("MQTT construct failed");
        rc = -1;
        release_buff();
    } else {
        aos_register_event_filter(EV_SYS,  mqtt_service_event, gpclient);
    }

    return rc;
}

int application_start(int argc, char *argv[])
{
#if AOS_ATCMD
    at.set_mode(ASYN);
    at.init(AT_RECV_PREFIX, AT_RECV_SUCCESS_POSTFIX,
            AT_RECV_FAIL_POSTFIX, AT_SEND_DELIMITER, 1000);
#endif

#ifdef WITH_SAL
    sal_init();
#endif

    printf("== Build on: %s %s ===\n", __DATE__, __TIME__);
    aos_set_log_level(AOS_LL_DEBUG);
    aos_register_event_filter(EV_WIFI, wifi_service_event, NULL);

    netmgr_init();
#if 0
    netmgr_ap_config_t apconfig;
    memset(&apconfig, 0, sizeof(apconfig));
    strcpy(apconfig.ssid, "LinkDevelop-Workshop");
    strcpy(apconfig.pwd, "linkdevelop");
    netmgr_set_ap_config(&apconfig);
#endif
    netmgr_start(false);
    aos_loop_run();

    return 0;
}
