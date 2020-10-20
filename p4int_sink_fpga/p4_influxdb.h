/**
 * @author Mario Kuka <kuka@cesnet.cz>
 *         Pavlina Patova <xpatov00@stud.fit.vutbr.cz>
 * @brief Header file of INT sink node
 * 
 * Copyright (c) 2015 - 2018 CESNET, z.s.p.o.
 */

#ifndef _P4_INFLUXDB_H_
#define _P4_INFLUXDB_H_

#include <sys/socket.h>

#include "InfluxDBFactory.h"
#include "p4int.h"

/**
 * Open the socket
 * \param socketfd Pointer to output socket variable
 * \param opt Options to be used
 * \return \ref RET_OK on success
 */
uint32_t p4_influxdb_open_socket(std::unique_ptr<influxdb::InfluxDB> &influxdb, const options_t* opt);

/**
 * Send packets to database
 * \param socketfd Socket file descriptor
 * \param payload Pointer to payload 
 * \param payload_len Length of the appended payload
 * \return \ref RET_OK on success
 */
uint32_t p4_influxdb_send_packet(std::unique_ptr<influxdb::InfluxDB> &influxdb, const telemetric_hdr_t& telemetric, const options_t* opt);

#endif // _P4_INFLUXDB_H_
