#ifndef FARMNODE_PARSER_H
#define FARMNODE_PARSER_H

#include <stdint.h>
#include "farmpulse_defs.h"

void app_packet_handler(uint8_t src_id, uint8_t type, uint8_t *msg, uint8_t len);
void fnSend_ND_Beacon(void);
void fnSend_Sensor_Telemetry(uint8_t target_id);
void fnSend_Heartbeat(void);
void fnTrigger_Alarm(alarm_code_t alarm_code, uint8_t severity, uint32_t fault_value);

#endif // FARMNODE_PARSER_H