#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#include "xtimer.h"
#include "net/emcute.h"
#include "net/ipv6/addr.h"
#include "msg.h"
#include "thread.h"
#include "shell.h"
#include "mutex.h"

#include "lpsxxx.h"
#include "lpsxxx_params.h"

#define DELAY (55000LU * US_PER_MS)
#define STATUS_LEN 5
#define MAIN_QUEUE_SIZE (8)
#define TOPIC_MAXLEN (128U)

typedef struct {
    int temperature;
    int pressure;
} SensorData;

static lpsxxx_t lpsxxx;
static char mqttStatus[STATUS_LEN] = "";

static msg_t mainMsgQueue[MAIN_QUEUE_SIZE];
static char stackMain[THREAD_STACKSIZE_DEFAULT];
static char stackLoop[THREAD_STACKSIZE_MAIN];
static kernel_pid_t mainThread;

static emcute_sub_t subscriptions[1];
static char topics[1][TOPIC_MAXLEN];

// Function declarations
static void *mqttThread(void *arg);
static void onMqttPublish(const emcute_topic_t *topic, void *data, size_t len);
static int publishToMqtt(char *topic, const char *data, int qos);
static const char *createJsonPayload(SensorData *sensors);
static int setupMqtt(void);
static void *mainLoop(void *arg);
static int commandStatus(int argc, char *argv[]);
static int commandMqttStatus(int argc, char *argv[]);

// Main function
int main(void) {
    msg_init_queue(mainMsgQueue, MAIN_QUEUE_SIZE);

    lpsxxx_init(&lpsxxx, &lpsxxx_params[0]);

    puts("FIT-IOT LAB sensor node to AWSIOT\n");
    puts("Setting up MQTT-SN.\n");
    setupMqtt();
    xtimer_sleep(3);

    puts("RIOT network stack Application");

    puts("[###STARTING MEASUREMENTS###]\n\n");
    thread_create(stackLoop, sizeof(stackLoop), THREAD_PRIORITY_MAIN - 1, 0, mainLoop, NULL, "mainLoop");
    puts("Thread started successfully!");

    char lineBuf[SHELL_DEFAULT_BUFSIZE];
    shell_run(shellCommands, lineBuf, SHELL_DEFAULT_BUFSIZE);
    return 0;
}

// Function definitions

static void *mqttThread(void *arg) {
    (void)arg;
    emcute_run(EMCUTE_DEFAULT_PORT, EMCUTE_ID);
    return NULL;
}

static void onMqttPublish(const emcute_topic_t *topic, void *data, size_t len) {
    char *incomingData = (char *)data;
    char command[len + 1];

    printf("### got publication for topic '%s' [%i] ###\n", topic->name, (int)topic->id);

    for (size_t i = 0; i < len; i++) {
        command[i] = incomingData[i];
    }
    command[len] = '\0';
    printf("%s", command);

    for (size_t i = 0; i < STATUS_LEN; i++) {
        if (i < len) {
            mqttStatus[i] = incomingData[i];
        } else {
            mqttStatus[i] = 0;
        }
    }

    puts("\n");
}

static int publishToMqtt(char *topic, const char *data, int qos) {
    emcute_topic_t mqttTopic;
    unsigned flags = EMCUTE_QOS_0;

    switch (qos) {
    case 1:
        flags |= EMCUTE_QOS_1;
        break;
    case 2:
        flags |= EMCUTE_QOS_2;
        break;
    default:
        flags |= EMCUTE_QOS_0;
        break;
    }

    mqttTopic.name = MQTT_TOPIC_OUT;
    if (emcute_reg(&mqttTopic) != EMCUTE_OK) {
        puts("PUB ERROR: Unable to obtain Topic ID");
        return 1;
    }

    if (emcute_pub(&mqttTopic, data, strlen(data), flags) != EMCUTE_OK) {
        printf("PUB ERROR: unable to publish data to topic '%s [%i]'\n", mqttTopic.name, (int)mqttTopic.id);
        return 1;
    }

    printf("PUB SUCCESS: Published %s on topic %s\n", data, topic);
    return 0;
}

static const char *createJsonPayload(SensorData *sensors) {
    static char json[128];
    sprintf(json, "{\"id\": \"%s\", \"temperature(°C)\": \"%i.%u\", \"Pressure(hPa)\": \"%d\"}",
                  EMCUTE_ID, ((sensors->temperature) / 100), ((sensors->temperature) % 100), sensors->pressure);
    return json;
}

static int setupMqtt(void) {
    memset(subscriptions, 0, sizeof(emcute_sub_t));

    xtimer_sleep(10);

    printf("Dev_id: %s\n", EMCUTE_ID);

    thread_create(stackMain, sizeof(stackMain), THREAD_PRIORITY_MAIN - 1, 0, mqttThread, NULL, "mqttThread");

    printf("Connecting to MQTT-SN broker %s port %d.\n", SERVER_ADDR, SERVER_PORT);

    sock_udp_ep_t gateway = {
        .family = AF_INET6,
        .port = SERVER_PORT
    };

    char *message = "connected";
    size_t messageLen = strlen(message);

    if (ipv6_addr_from_str((ipv6_addr_t *)&gateway.addr.ipv6, SERVER_ADDR) == NULL) {
        printf("error parsing IPv6 address\n");
        return 1;
    }

    if (emcute_con(&gateway, true, MQTT_TOPIC_OUT, message, messageLen, 0) != EMCUTE_OK) {
        printf("error: unable to connect to [%s]:%i\n", SERVER_ADDR, (int)gateway.port);
        return 1;
    }

    printf("Successfully connected to gateway at [%s]:%i\n", SERVER_ADDR, (int)gateway.port);

    subscriptions[0].cb = onMqttPublish;
    strcpy(topics[0], MQTT_TOPIC_IN);
    subscriptions[0].topic.name = MQTT_TOPIC_IN;

    if (emcute_sub(&subscriptions[0], EMCUTE_QOS_0) != EMCUTE_OK) {
        printf("error: unable to subscribe to %s\n", MQTT_TOPIC_IN);
        return 1;
    }

    printf("Now subscribed to %s\n", MQTT_TOPIC_IN);

    return 1;
}

static void *mainLoop(void *arg) {
    (void)arg;
    mainThread = thread_getpid();
    SensorData sensors;

    xtimer_ticks32_t last = xtimer_now();
    while (1) {
        srand(time(NULL));
        lpsxxx_read_temp(&lpsxxx, &sensors.temperature);
        lpsxxx_read_pres(&lpsxxx, &sensors.pressure);

        printf("Pressure: %uhPa\n", sensors.pressure);
        printf("Temperature: %i.%u°C\n", (sensors.temperature / 100), (sensors.temperature % 100));

        printf("[MQTT] Publishing data to MQTT Broker\n");
        publishToMqtt(MQTT_TOPIC_OUT, createJsonPayload(&sensors), 0);
        xtimer_sleep(2);

        printf("\n");

        xtimer_periodic_wakeup(&last, DELAY);
    }
}

static int commandStatus(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    printf("WELCOME!!!\n");
    return 0;
}

static int commandMqttStatus(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    printf("MQTT BROKER ADDRESS: %s \nPORT: %d \nTOPIC_IN: %s/%s TOPIC_OUT: %s\n", SERVER_ADDR, SERVER_PORT, MQTT_TOPIC_IN, EMCUTE_ID, MQTT_TOPIC_OUT);
    return 0;
}

static const shell_command_t shellCommands[] = {
    { "status", "get a status report", commandStatus },
    { "mqtt_status", "mqtt status report", commandMqttStatus },
    { NULL, NULL, NULL }
};
