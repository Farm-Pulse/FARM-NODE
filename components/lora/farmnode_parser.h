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
void fnSet_Device_ID(uint8_t target_id, uint8_t new_id);
void fnGet_Device_ID(uint8_t target_id);
void fnPoll_Sensors_Background(void);
void fnSend_Unified_Sensor_Data(uint8_t target_id);
void fnSet_Motor_Relay(uint8_t target_id, uint8_t motor_action);
void fnSet_Network_ID(uint8_t target_id, uint8_t nwk_id_msb, uint8_t nwk_id_lsb);
void fnSend_Motor_State_Resp(uint8_t target_id);
void fnSend_Soil_Data_Resp(uint8_t target_id);
void fnSend_Temp_Data_Resp(uint8_t target_id);
void fnSend_Soil_Temp_Resp(uint8_t target_id);
#endif // FARMNODE_PARSER_H