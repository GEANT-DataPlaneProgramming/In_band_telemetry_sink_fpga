/**
 * @author Mario Kuka <kuka@cesnet.cz>
 *         Pavlina Patova <xpatov00@stud.fit.vutbr.cz>
 * @brief Header file of INT sink node
 * 
 * Copyright (c) 2015 - 2018 CESNET, z.s.p.o.
 */

#include <string>
#include <iostream>
#include <errno.h> 
#include <time.h>
#include <sys/time.h>
#include <thread> 

#include "p4_influxdb.h"

/**
 * Write error message to log file
 * \param opt Program options
 * \param err Error message 
 */
static void write_to_log_file(const options_t *opt, const std::string err) {
    time_t rawtime;
    struct tm *timeinfo;
    time(&rawtime);
    timeinfo = localtime(&rawtime);
    FILE* file = fopen(opt->logFile, "a");
    if(file != NULL) {
        fprintf(file,"%s: %s\n", asctime(timeinfo), err.c_str());
    }
    fflush(file);
    fclose(file);
}

/**
 * Open the socket
 * \param socketfd Pointer to output socket variable
 * \param opt Options to be used
 * \return \ref RET_OK on success
 */
static uint32_t p4_influxdb_open_socket(std::unique_ptr<influxdb::InfluxDB> &influxdb, const options_t* opt) {
    std::string host(opt->host);
    std::string port = std::to_string(opt->port);
    std::string protocol(opt->protocol);
    std::string username(opt->username);
    std::string passwd(opt->password);

    // "[protocol]://[username:password@]host:port[/?db=database]"
    std::string conString = protocol + "://" + username + ":" + passwd + "@" + 
        host + ":" + port + "/?db=int_telemetry";

    try {
        influxdb = influxdb::InfluxDBFactory::Get(conString);  
        influxdb->batchOf(opt->batch);  
    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        influxdb = NULL;
        return RET_ERR;
    }
    return RET_OK;
}



/**
 * Convert raw data to the int reports and send them to the influxdb
 * \param data Raw int data
 * \param opt Program options
 */
static void fill(std::vector<telemetric_hdr_t> data, const options_t* opt)
{
    if(opt->hostValid) {
        // Initialize connection to the influxdb
        std::unique_ptr<influxdb::InfluxDB> influxdb = nullptr;
        int ret = p4_influxdb_open_socket(influxdb, opt);
        if(ret != RET_OK) {
            write_to_log_file(opt, "Unable to connect to the Inxlux DB");
            return;
        }
    
        try {
            for(auto &telemetric : data) {
                // Send report
                influxdb->write(influxdb::Point{"int_telemetry"}
                    .addTag("srcip",std::string(telemetric.srcIp))
                    .addTag("dstip",std::string(telemetric.dstIp))
                    .addTag("srcp", std::to_string(telemetric.srcPort))
                    .addTag("dstp", std::to_string(telemetric.dstPort))
                    .addField("origts", static_cast<long long int>(telemetric.origTs))
                    .addField("dstts", static_cast<long long int>(telemetric.dstTs))
                    .addField("seq", static_cast<long long int>(telemetric.seqNum))
                    .addField("delay", static_cast<long long int>(telemetric.delay))
                    .addField("sink_jitter", static_cast<long long int>(telemetric.sink_jitter))
                    .addField("reordering", static_cast<long long int>(telemetric.reordering))
                    .setNanoTime(static_cast<long long int>(telemetric.dstTs)));
            }
            // flush buffer 
            influxdb->flushBuffer();

        } catch (const std::runtime_error& e) {
            if(opt->log == 1) {
                write_to_log_file(opt, e.what());
            }
        } catch (...) {}            
    }
}

static std::vector<telemetric_hdr_t> raw_buff; // raw int data buffer
uint32_t p4_influxdb_send_packet(const telemetric_hdr_t& telemetric, const options_t* opt) {
    raw_buff.push_back(telemetric);
    if(raw_buff.size() == opt->raw_buffer) {
        std::thread(fill, raw_buff, opt).detach();
        raw_buff.clear();
    }

    return RET_OK;
}

