/**
 * @author Mario Kuka <kuka@cesnet.cz>
 *         Pavlina Patova <xpatov00@stud.fit.vutbr.cz>
 * @brief Header file of INT sink node
 */

#ifndef _P4INT_H_
#define _P4INT_H_

#include <cstdint>
#include <stdio.h>
#include <vector>
#include <array>

// Success return code
#define RET_OK 0
// Error return code
#define RET_ERR 1
// Packet is not available
#define RET_NO_PKT 2 
// Not enough space for the packet 
#define RET_PKT_SIZE 3 
// Size of char buffers 
#define CHAR_BUFF_SIZE 512
// Size of the NDP packet buffer 
#define NDP_PACKET_BUFF 32
// Size of the charatecter buffer inside the telemetric structure 
#define IP_BUFF_SIZE 17

// Configuration of the program 
typedef struct {
    uint32_t devId;                    // Device ID 
    char     host[CHAR_BUFF_SIZE];     // Host address of the collector 
    uint8_t  hostValid;                // Valid of host address  
    uint16_t port;                     // Host destination port 
    char     protocol[CHAR_BUFF_SIZE]; // Host transfer protocol  
    char     username[CHAR_BUFF_SIZE]; // Host username  
    char     password[CHAR_BUFF_SIZE]; // Host password  
    uint32_t batch;                    // How many packets send at once
    uint8_t  log;                      // Enable log file 
    char     logFile[CHAR_BUFF_SIZE];  // Path of the log file 
    uint8_t  verbose;                  // Print parsed data 
    uint8_t  tstmp;                    // Enables 48-bit timestamp mod 
    uint8_t  p4cfg;                    // Configure P4 device
    uint32_t smpl_rate;                // Sampling rate
    uint32_t raw_buffer;               // Size of buffer for raw int data
    std::vector<std::array<uint8_t, 6>> ip_flt; // Filter this flows (srouce ip and destination port)
    char    interface[CHAR_BUFF_SIZE];  // Interface designed to catch traffic
} options_t;

// Structure with telemetric information to export 
typedef struct {
   char        srcIp[IP_BUFF_SIZE]; // String - source IPv4 address
   char        dstIp[IP_BUFF_SIZE]; // String - destination IPv4 address
   uint16_t    srcPort;             // Value - source port
   uint16_t    dstPort;             // Value - destination port
   uint64_t    origTs;              // Value - orig. timestamp (UNIX NS format)
   uint64_t    dstTs;               // Value - dest. timestamp (UNIX NS format)
   uint64_t    seqNum;              // Sequence number of the received frame
   uint64_t    delay;               // Difference between the dest. and orig. timestamp
   uint64_t    sink_jitter;         // Difference between the dest timestamp of current packet and the previous
   int64_t     reordering;              
} telemetric_hdr_t;

// Flow metadata structure 
struct meta_data {
    uint64_t seq = 0;        // Current sequence number
    uint64_t prev_dstTs = 0; // Previous destination timestamp
};

struct int_influx_t{
    uint32_t  srcAddr;
    uint32_t  dstAddr;
    uint16_t  ingress_port_id;
    uint16_t  egress_port_id;
    uint8_t   meta_len;
    uint8_t   rsvd1[3];
    uint32_t  ndk_tstamp1;
    uint32_t  ndk_tstamp2;
    uint64_t  delay;
    uint32_t  seq;
}__attribute__((packed));  

struct int_meta_t{
    uint32_t switch_id;
    uint16_t ingress_port_id;
    uint16_t egress_port_id;
    uint64_t ingress_tstamp;
    uint64_t egress_tstamp;
}__attribute__((packed));


/**
 * Sleep in microseconds
 */
void delay_usecs(unsigned int us);

#endif //_P4_INT_H_