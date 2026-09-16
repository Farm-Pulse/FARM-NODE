# FarmPulse LoRa RF Frame Architecture (Industrial Standard)

This document outlines the strict binary LoRa RF frame formats used for over-the-air communication between the FarmGateway and FarmNodes. It serves as the exact data contract between the Gateway Team (parsing/building MQTT) and the Node Team (parsing/building RF).

## 1. The Fixed 10-Byte Network Header (All Frames)

Every single packet transmitted over the LoRa network begins with this 10-byte routing header.

| Byte Offset | Field Name | Size | Description |
| :--- | :--- | :--- | :--- |
| `[0]` | **Length** | 1 Byte | Total length of the packet (Header + Payload). |
| `[1]` | **Target ID** | 1 Byte | Immediate next-hop Node ID receiving this packet. |
| `[2]` | **Sender ID** | 1 Byte | Immediate previous-hop Node ID sending this packet. |
| `[3-4]` | **Network PAN ID** | 2 Bytes| Personal Area Network ID (e.g., `0xA1B2`) separating farms. |
| `[5]` | **Sequence Num** | 1 Byte | Rolling counter (0-255) to detect duplicate packets. |
| `[6]` | **FCF** | 1 Byte | Bitmask: Extended Header, Encryption, AckReq, Pkt Type. |
| `[7]` | **Hop Count** | 1 Byte | Time-To-Live (TTL) counter. Decrements on each hop. |
| `[8]` | **Final Dest ID** | 1 Byte | Ultimate destination Node ID. |
| `[9]` | **Origin Src ID** | 1 Byte | Node ID that originally created the packet. |

---

## 2. DOWNLINK: SET Frame (Motor ON/OFF)
**Flow:** FarmGateway -> FarmNode
**Trigger:** Web Dashboard "Turn Motor ON/OFF" button.
**MAC FCF Packet Type:** `PKT_TYPE_CMD` (`0x00`)

**Payload Structure (5 Bytes):**
| Offset | Hex Value | Name | Description |
| :--- | :--- | :--- | :--- |
| `[0]` | `0x05` | `CMD_TYPE_CONFIG` | Indicates a Configuration/Control command. |
| `[1]` | `0x01` | `_TYPE_SEND_CMD` | Direction: Sending a command request. |
| `[2]` | `0x01` | `ACTION_SET` | Action: Write/Set a value. |
| `[3]` | `0x02` | `PARAM_MOTOR_CTRL` | Target Parameter: The Motor Relay. |
| `[4]` | `0x00`/`0x01` | `Data` | `0x01` = Turn Motor ON, `0x00` = Turn Motor OFF. |

---

## 3. DOWNLINK: GET Frames

### 3A. Request Sensor Reading
**Flow:** FarmGateway -> FarmNode
**Trigger:** Gateway polling routine or User clicking "Refresh Data".
**MAC FCF Packet Type:** `PKT_TYPE_CMD` (`0x00`)

**Payload Structure (5 Bytes):**
| Offset | Hex Value | Name | Description |
| :--- | :--- | :--- | :--- |
| `[0]` | `0x05` | `CMD_TYPE_CONFIG` | Indicates a Configuration/Control command. |
| `[1]` | `0x01` | `_TYPE_SEND_CMD` | Direction: Sending a command request. |
| `[2]` | `0x02` | `ACTION_GET` | Action: Read/Get a value. |
| `[3]` | `0x1E` | `PARAM_METER_DATA_REQ`| Target Parameter: 3-Phase Meter Data. |
| `[4]` | `0x01` | `Mode` | `0x01` = Unicast immediate response. |

### 3B. Request Motor Status
**Flow:** FarmGateway -> FarmNode
**Trigger:** Gateway requesting current relay state.
**MAC FCF Packet Type:** `PKT_TYPE_CMD` (`0x00`)

**Payload Structure (4 Bytes):**
| Offset | Hex Value | Name | Description |
| :--- | :--- | :--- | :--- |
| `[0]` | `0x05` | `CMD_TYPE_CONFIG` | Indicates a Configuration/Control command. |
| `[1]` | `0x01` | `_TYPE_SEND_CMD` | Direction: Sending a command request. |
| `[2]` | `0x02` | `ACTION_GET` | Action: Read/Get a value. |
| `[3]` | `0x02` | `PARAM_MOTOR_CTRL` | Target Parameter: Motor Status. |


### 3C. Unified Panel Status / Periodic Frame (PARAM_LORA_PANEL_STATUS)
Triggered periodically or via GET request to deliver all system parameters, 3-phase electrical metrics, and 8-bit environmental data simultaneously[cite: 14].

| Offset | Size | Name | Description |
| :--- | :--- | :--- | :--- |
| `[0-3]` | 4 Bytes | `Headers` | `0x05 0x02 0x02` + `PARAM_LORA_PANEL_STATUS` |
| `[4]` | 1 Byte | `Node ID` | FarmNode Origin ID (DA-[B]) |
| `[5]` | 1 Byte | `Firmware` | Version Hex (DA-[C]) |
| `[6]` | 1 Byte | `Reg/Chan`| Region & Channel No (DA-[D]) |
| `[7-8]` | 2 Bytes | `Net ID` | Network PAN ID (DA-[E]) |
| `[9-16]` | 8 Bytes | `MAC ID` | UUID of the ESP32 (DA-[F]) |
| `[17]` | 1 Byte | `Reboots` | Power ON Interruption Count (DA-[G]) |
| `[18]` | 1 Byte | `Motor` | Relay Status: 1=ON, 0=OFF (DA-[H]) |
| `[19-24]`| 6 Bytes | `V_RYB` | ZMPT 3-Phase Voltages (DA-[I, J, K]) |
| `[25]` | 1 Byte | `Air Temp`| DHT22 Temperature in °C (DA-[L]) |
| `[26]` | 1 Byte | `Air Hum` | DHT22 Humidity % (DA-[M]) |
| `[27]` | 1 Byte | `Soil Temp`| DS18B20 Soil Temperature in °C (DA-[N]) |
| `[28]` | 1 Byte | `Reserved`| System expansion slot (DA-[O]) |
| `[29-32]`| 4 Bytes | `Alarms` | Bitmasked Alarm Registers 1 & 2 (DA-[P, Q]) |
| `[33]` | 1 Byte | `Neighbors`| Neighbor Count (DA-[R]) |
| `[34+]` | N Bytes | `Routing` | Neighbor IDs (DA-[S, T...]) |

---

## 4. UPLINK: Periodic & Telemetry Frames

### 4A. Periodic "I'm Alive" Heartbeat (Normal HB)
**Flow:** FarmNode -> FarmGateway (every T minutes)
**Trigger:** Node's internal periodic timer to update neighbor tables and connection status.
**MAC FCF Packet Type:** `PKT_TYPE_STATUS` (`0x02`)

**Payload Structure (Variable Length):**
| Offset | Size | Name | Description |
| :--- | :--- | :--- | :--- |
| `[0]` | 1 Byte | `Sub-Status` | `0x02` = NORMAL_HB |
| `[1]` | 1 Byte | `RSSI` | RSSI with Parent Hop (dBm) |
| `[2]` | 1 Byte | `Hop Count` | Hop Count traversed |
| `[3]` | 1 Byte | `Motor State`| `0x01` = ON, `0x00` = OFF |
| `[4]` | 1 Byte | `Neighbor Count` | Number of connected neighbors (N) |
| `[5 to 5+N]`| N Bytes| `Neighbor IDs` | Array of Neighbor Node IDs |

### 4B. Periodic / Response Frame (Sensor Telemetry)
**Flow:** FarmNode -> FarmGateway
**Trigger:** Node's internal periodic timer OR in response to a `GET` Frame.
**MAC FCF Packet Type:** `PKT_TYPE_DATA` (`0x01`)

**Payload Structure (24 Bytes - Packed C-Struct):**
| Offset | Size | Name | Scaling / Format |
| :--- | :--- | :--- | :--- |
| `[0-1]` | 2 Bytes | `voltage_R` | Raw integer (Volts) |
| `[2-3]` | 2 Bytes | `voltage_Y` | Raw integer (Volts) |
| `[4-5]` | 2 Bytes | `voltage_B` | Raw integer (Volts) |
| `[6-7]` | 2 Bytes | `current_R` | Amps x 10 (e.g., `152` = 15.2A) |
| `[8-9]` | 2 Bytes | `current_Y` | Amps x 10 |
| `[10-11]`| 2 Bytes | `current_B` | Amps x 10 |
| `[12-15]`| 4 Bytes | `power_active` | Watts |
| `[16-17]`| 2 Bytes | `frequency` | Hz x 10 (e.g., `500` = 50.0Hz) |
| `[18-19]`| 2 Bytes | `power_factor` | PF x 1000 (e.g., `980` = 0.98) |
| `[20]` | 1 Byte | `motor_status` | `0` = OFF, `1` = ON, `2` = Tripped |
| `[21]` | 1 Byte | `fault_mask` | Bitmask of active alarms |
| `[22-23]`| 2 Bytes | `supply_voltage` | Node internal battery/DC Bus in mV |

---

## 5. UPLINK: Fault & Alarm Frames
**Flow:** FarmNode -> FarmGateway
**Trigger:** Triggered immediately upon sensor anomaly or hardware protection trip.
**MAC FCF Packet Type:** `PKT_TYPE_FAULT` (`0x09`)

**Payload Structure (6 Bytes):**
| Offset | Size | Name | Description |
| :--- | :--- | :--- | :--- |
| `[0]` | 1 Byte | `Alarm Code` | Identification of the fault (see below). |
| `[1]` | 1 Byte | `Severity` | `0x01` = Warning, `0x02` = Critical Auto-Trip. |
| `[2-5]` | 4 Bytes| `Fault Value`| **Upper 16 Bits:** Phase ID (0=All, 1=R, 2=Y, 3=B) <br> **Lower 16 Bits:** Tripping Voltage |

---

## 6. MAC-Layer ACK Frame (Delivery Receipt)
**MAC FCF Packet Type:** `PKT_TYPE_ACK` (`0x03` / `0x04`)

| Offset | Size | Name | Description |
| :--- | :--- | :--- | :--- |
| `[0]` | 1 Byte | `Seq Num` | The sequence number of the packet being acknowledged. |

**Supported Alarm Codes:**
*   `0x01`: Phase Voltage Loss (R/Y/B missing)
*   `0x02`: Motor Overcurrent / Jammed Impeller
*   `0x03`: Dry Run (Under-current condition)
*   `0x04`: Frequency Instability (<45Hz or >55Hz)
*   `0x05`: LoRa Jamming / Communication Failure
