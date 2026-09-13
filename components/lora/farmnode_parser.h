#ifndef FARMNODE_PARSER_H
#define FARMNODE_PARSER_H

#include <stdint.h>
#include "farmpulse_defs.h"

void app_packet_handler(uint8_t src_id, uint8_t type, uint8_t *msg, uint8_t len);
void fnSend_ND_Beacon(void);
void fnSend_Sensor_Telemetry(uint8_t target_id);
void fnSend_Heartbeat(uint8_t target_id);
void fnTrigger_Alarm(alarm_code_t alarm_code, uint8_t severity, uint32_t fault_value);
void fnSend_Panel_Status(uint8_t target_id);
void fnCheck_Phase_Loss(void);
static void fnSet_Device_ID(uint8_t target_id, uint8_t new_id);
static void fnGet_Device_ID(uint8_t target_id);
void fnPoll_Sensors_Background(void);
#endif // FARMNODE_PARSER_H