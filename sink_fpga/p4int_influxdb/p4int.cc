/**
 * @author Mario Kuka <kuka@cesnet.cz>
 *         Pavlina Patova <xpatov00@stud.fit.vutbr.cz>
 * @brief INT sink node
 *     
 * Copyright (c) 2015 - 2018 CESNET, z.s.p.o.
 */

#include <cstdio>
#include <getopt.h>
#include <errno.h>
#include <cstring>
#include <cinttypes>
#include <signal.h>
#include <arpa/inet.h>
#include <ctime>
#include <cmath>
#include <map>

#include "device.h"
#include "p4int.h"

/**
 * Packet counter
 */
uint64_t pkt_cnt = 0;

/**
 * Helping control variable
 */
volatile sig_atomic_t stop = 0;

/**
 * Flow metadata
 */
static std::map<uint64_t, meta_data> flow_map;

/**
 * Setup the stop flag 
 */
void setup_stop(int sig) {
    printf("%lu\n", pkt_cnt);
    stop = 1;
}

/**
 * Sleep in microseconds
 */
static void delay_usecs(unsigned int us)
{
    struct timespec t1;
    struct timespec t2;

    if (us == 0)
        return;

    t1.tv_sec = (us / 1000000);
    t1.tv_nsec = (us % 1000000) * 1000;

    // NB: Other variants of sleep block whole process.
retry:
    if (nanosleep((const struct timespec *)&t1, &t2) == -1)
        if (errno == EINTR) {
            t1 = t2; 
            goto retry;
        }
    return;
}

/**
 * Convert the passed timestamp in TS NS to Unix NS
 * \param tsNs input timestamp in the TS NS format
 * \return Converted timestamp in the UNIX NS format
 */
static inline uint64_t convertToUnixNs(uint64_t tsNs) {
    uint32_t sec = tsNs >> 32;
    uint32_t nanosec = tsNs & 0xffffffff;
    // Convert seconds to nanoseconds and add the nanosecond part
    return (sec * 100000000000) + nanosec;
}

/**
 * Convert the network order to 64-bit host order
 * \param input Input network order  
 * \return Converted number
 */
uint64_t ntoh64(uint64_t value) {
    uint64_t rval;
    const uint64_t* input = &value;
    uint8_t *data = (uint8_t *)&rval;

    data[0] = *input >> 56;
    data[1] = *input >> 48;
    data[2] = *input >> 40;
    data[3] = *input >> 32;
    data[4] = *input >> 24;
    data[5] = *input >> 16;
    data[6] = *input >> 8;
    data[7] = *input >> 0;

    return rval;
}

/**
 * Print telemetric data
 */
void print_telemetric(const telemetric_hdr_t *hdr) {
    printf("Orig TS       => %lu\n", hdr->origTs);
    printf("Dest TS       => %lu\n", hdr->dstTs);
    printf("Seq           => %lu\n", hdr->seqNum);
    printf("Delay         => %lu\n", hdr->delay);
    printf("Sink Jitter   => %lu\n", hdr->sink_jitter);
    printf("IP Src        => %s\n",  hdr->srcIp);
    printf("IP Dst        => %s\n",  hdr->dstIp);
    printf("Src Port      => %hu\n", hdr->srcPort);
    printf("Dst Port      => %hu\n", hdr->dstPort);
}

/**
 * Process one received packet based on the program
 * \param pkt Input packet to prs
 * \param influxdb Database descriptor
 * \param opt Program parameters
 * \return RET_OK if everything was fine
 */
uint32_t process_packet(struct ndp_packet& pkt, std::unique_ptr<influxdb::InfluxDB> &influxdb, const options_t& opt) {
    // Prepare telemetric data into the apropriate structure
    telemetric_hdr_t tmpHdr;

    // Convert source timestamp
    tmpHdr.origTs = ntoh64(*((uint64_t*) (pkt.data+12)));
    // Convert destination timestamp
    tmpHdr.dstTs = ntohl((*(uint32_t*)(pkt.data +32))) ;
    tmpHdr.dstTs += ntohl((*((uint32_t*)(pkt.data+28)))) *  1000000000ll; 
    // Cut of timestamps to 48 bits
    if(opt.tstmp == 1) {
        uint64_t mask = 0x0000FFFFFFFFFFFF;
        tmpHdr.origTs = tmpHdr.origTs & mask;
        tmpHdr.dstTs = tmpHdr.dstTs & mask;
    }

    // Convert IP addresses 
    inet_ntop(AF_INET, pkt.data, tmpHdr.srcIp, IP_BUFF_SIZE);
    inet_ntop(AF_INET, pkt.data + 4, tmpHdr.dstIp, IP_BUFF_SIZE);

    // Convert source and destination ports
    tmpHdr.srcPort =  ntohs(*((uint16_t*) (pkt.data + 8)));
    tmpHdr.dstPort =  ntohs(*((uint16_t*) (pkt.data + 10)));

    // Get flow data
    // TODO: Implement flow hash table 
    uint64_t map_key = *((uint64_t*)(pkt.data));
    meta_data &meta_tmp = flow_map[map_key];
   
    // Calculate int header
    tmpHdr.delay = tmpHdr.dstTs - tmpHdr.origTs;
    tmpHdr.seqNum = meta_tmp.seq; 
    tmpHdr.sink_jitter = tmpHdr.dstTs - meta_tmp.prev_dstTs;
    tmpHdr.reordering = 0;   
    
    // Update flow data
    meta_tmp.prev_dstTs = tmpHdr.dstTs;
    meta_tmp.seq++;

    // Report to influxdb
    if(opt.hostValid && (pkt_cnt % opt.smpl_rate == 0)) {
        uint32_t ret = p4_influxdb_send_packet(influxdb,tmpHdr, &opt);
        if(ret != RET_OK) {
            printf("Error during the export to InfluxDB\n");
            return RET_ERR;
        }
    }  
      
    // Print to console
    if(opt.verbose) {
        print_telemetric(&tmpHdr);
    }

    return RET_OK;
}

/** 
 * Print the help
 */
void print_help(const char* prgname){
    printf("%s [-d device] [-c collectorAddress] [-p collectorPort] [-r collectorProtocol]" 
           " [-u username] [-s password] [-b numOfReports] [-l logFile] [-m samplingRate]"
           " [-vtkh]\n", prgname);
    printf("\t* -d = ID of the device (e.g.,0 stands for /dev/nfb0, default is 0).\n");
    printf("\t* -c = Host address of the collector.\n");
    printf("\t* -p = Port of collector.\n");
    printf("\t* -r = Protocol of collector.\n");
    printf("\t* -u = Username of collector.\n");
    printf("\t* -s = Password of collector.\n");
    printf("\t* -b = How many reports send at once (default is 1000).\n"); 
    printf("\t* -l = Error messages will be written to given log file.\n"); 
    printf("\t* -m = Set sampling rate of reporting to database (default is 1).\n"); 
    printf("\t* -v = Enable the verbose mode for printinf of parsed data.\n"); 
    printf("\t* -t = Enable 48-bit timestamp mode.\n");
    printf("\t* -k = Force P4 device configuration.\n");
    printf("\t* -h = Prints the help.\n");
}

/**
 * Parse arguments and prepare the configuration
 *
 * \param opt Poiter to the \ref options_t strcture
 * \param argc Number of passed arguments
 * \param argv Passed values of arguments
 * \return \ref RET_OK on sucess
 */
static int32_t parse_arguments(options_t* opt, int32_t argc, char** argv) {
    // Setup default parameters
    opt->devId = 0;
    opt->hostValid = 0;
    opt->batch = 1000;
    opt->log = 0;
    opt->verbose = 0;
    opt->tstmp = 0;   
    opt->p4cfg = 0;
    opt->smpl_rate = 1; 

    int32_t op;
    char* tmp;
    
    // Parse all parameters
    while((op = getopt(argc, argv, "d:c:p:r:u:s:b:l:m:vtkh")) != -1) {
        switch(op) {
            case 'd':
                // Parse the device ID
                opt->devId = strtoumax(optarg, &tmp, 10);
                if(errno == ERANGE) {
                    printf("Device id is higher than 32bits!");
                    return RET_ERR;
                }                
                break;                
            
            case 'c':
                // Copy the Host address
                strcpy(opt->host, optarg);
                opt->hostValid = 1;
                break;
            
            case 'p':
                // Parse the collector port 
                opt->port = strtoumax(optarg, &tmp, 10);
                if(errno == ERANGE || opt->port > UINT16_MAX) {
                    printf("Port is higher than 16bits!");
                    return RET_ERR;
                }                
                break;                
            
            case 'r':
                // Copy the Host protocol
                strcpy(opt->protocol, optarg);
                break;
            
            case 'u':
                // Copy the Host username
                strcpy(opt->username, optarg);
                break;
            
            case 's':
                // Copy the Host password 
                strcpy(opt->password, optarg);
                break;
    
            case 'b':
                // Size of batch
                opt->batch = atoi(optarg);
                break;

            case 'l':
                // Log file
                opt->log = 1;
                strcpy(opt->logFile, optarg);
                break;
            
            case 'm':
                // Size of batch
                opt->smpl_rate = atoi(optarg);
                break;
            
            case 'v':
                // Verbose mode, print parsed data
                opt->verbose = 1;
                break;
            
            case 't':
               // Enable 48-bit timestamp 
               opt->tstmp = 1;
               break;
            
            case 'k':
               // Configure P4 device
               opt->p4cfg = 1;
               break;
            
            case 'h':
                print_help(argv[0]);
                exit(0);

            case '?':
                // Unknown parameter, print help and end
                print_help(argv[0]);
                return RET_ERR;
        } 
    }
    return RET_OK;
}

/**
 * Processing all incoming packets
 * \param device P4 device
 * \param nfb NFB device
 * \param influxdb Influx database
 * \param opt Program parameters
 * \return RET_OK if everything was fine
 */
int loop_proccess(p4device_t &device, nfb_int_dev_t &nfb, std::unique_ptr<influxdb::InfluxDB> &influxdb,
        options_t &opt)
{
    uint32_t pkt_rx_ret;
    uint32_t ret_pkt_proc;
    struct ndp_packet packets[NDP_PACKET_BUFF]; 
    uint32_t empty_buff = 0;
    
    while(!stop) {
        // Read the packet from the buffer
        pkt_rx_ret = ndp_rx_burst_get(nfb.rx, packets, NDP_PACKET_BUFF);
    
        // flush influxdb buffer
        if(pkt_rx_ret == 0) {
            empty_buff++;
            if(empty_buff == 100 && influxdb != nullptr) {
                try {   
                    influxdb->flushBuffer();
                } catch(...) {}
            }
            delay_usecs(100);
            continue;
        } else {
            empty_buff = 0;
        }

        // Process all packets 
        for(uint8_t i = 0; i < pkt_rx_ret; i++) {
            // Increment counter for each finished one 
            pkt_cnt++;
            // Process packet
            ret_pkt_proc = process_packet(packets[i], influxdb, opt);
            if(ret_pkt_proc != RET_OK) {
                printf("Error during the packet processing!\n");
                close_device(&device, &opt, &nfb);
                return RET_ERR;
            }
        }

        // Mark all read packets as finished   
        if(pkt_rx_ret != 0) {
            ndp_rx_burst_put(nfb.rx); 
        }
    }
   
    close_device(&device,&opt,&nfb);
    return RET_OK;
}


int32_t main(int32_t argc, char** argv) {
    // Prepare the configuration
    int32_t ret;
    options_t opt;
    if(parse_arguments(&opt, argc, argv) != RET_OK) {
        return RET_ERR;
    }
    
    // Prepare influxdb
    std::unique_ptr<influxdb::InfluxDB> influxdb = nullptr;
    if(opt.hostValid) {
        ret = p4_influxdb_open_socket(influxdb, &opt);
        if(ret != RET_OK) {
            printf("Unable to connect to the Inxlux DB\n");
            return RET_ERR;
        }
    }
    
    // Prepare device 
    p4device_t device;
    nfb_int_dev_t nfb;
    ret = open_device(&device, &opt, &nfb);
    if(ret != RET_OK) {
        // Close all already opened parts
        close_device(&device, &opt, &nfb);
        return RET_ERR;
    }

    // Configure the device
    if(opt.p4cfg) {
        ret = configure_device(&device,&opt);
        if(ret != RET_OK) {
            close_device(&device,&opt,&nfb);
            return RET_ERR;
        }
    }

    // Register signal to enable catching of Ctrl+c
    if(signal(SIGINT, setup_stop) == SIG_ERR) {
        printf("Unable to register SIGINT handler!\n");
        close_device(&device, &opt, &nfb);
        return RET_ERR;
    }
   
    // infinite loop packet processing
    ret = loop_proccess(device, nfb, influxdb, opt);
    
    return ret;
}
