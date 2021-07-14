/**
 * @author Mario Kuka <kuka@cesnet.cz>
 *         Pavlina Patova <xpatov00@stud.fit.vutbr.cz>
 * @brief INT sink node
 */

#include <iostream>
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
#include <fstream>
#include <inttypes.h>
    
#include "device.h"
#include "p4int.h"
#include "p4_influxdb.h"

/**
 * Structures for handling packet data nicier
 */
struct int_influx_t{
      uint32_t  srcAddr;
      uint32_t  dstAddr;
      uint16_t  ingress_port_id;
      uint16_t  egress_port_id;
      uint8_t   meta_len;
      uint8_t   hop_meta_len;
      uint8_t   rsvd1[2];
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

#define TCP  6
#define UDP 17

/**
 * Auxiliary variables for node jitter
 */
#define MAX_NODES 10
uint64_t prev_timestamps[MAX_NODES];

/**
 * Packet counter
 */
uint64_t pkt_cnt = 0;

/**
 * Packet drop counter
 */
uint64_t pkt_drop = 0;

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
    printf("\ntotal - %lu\ndrop - %lu\n", pkt_cnt, pkt_drop);
    stop = 1;
}

/**
 * Sleep in microseconds
 * \param us Number of microseconds
 */
void delay_usecs(unsigned int us) {
    struct timespec t1;
    struct timespec t2;

    if (us == 0) {
        return;
    }

    t1.tv_sec = (us / 1000000);
    t1.tv_nsec = (us % 1000000) * 1000;

    // NB: Other variants of sleep block whole process.
retry:
    if (nanosleep((const struct timespec *)&t1, &t2) == -1) {
        if (errno == EINTR) {
            t1 = t2; 
            goto retry;
        }
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
 * \param hdr Structureof telemetric data
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
 * Write headre information to format sutable for sending
 * \param tmpHdr Where to store parsed information
 * \param int_hdr Raw data from packet
 * \param opt Program parameters
 * \param map_key Map key
 */
void get_int_header_data(telemetric_hdr_t &tmpHdr, struct int_influx_t *int_hdr, uint64_t map_key) {
    // Convert destination timestamp
    tmpHdr.dstTs = ntohl(((int_hdr->ndk_tstamp2)));
    tmpHdr.dstTs += ntohl(((int_hdr->ndk_tstamp1)))*  1'000'000'000ll;

    // Convert IP addresses 
    inet_ntop(AF_INET, &int_hdr->srcAddr, tmpHdr.srcIp, IP_BUFF_SIZE);
    inet_ntop(AF_INET, &int_hdr->dstAddr, tmpHdr.dstIp, IP_BUFF_SIZE);

    // Convert source and destination ports
    tmpHdr.srcPort =  ntohs(((int_hdr->ingress_port_id)));
    tmpHdr.dstPort =  ntohs(((int_hdr->egress_port_id)));

    // Get flow data
    // TODO: Implement flow hash table   
    meta_data &meta_tmp = flow_map[map_key];

    // Calculate int header
    tmpHdr.delay = tmpHdr.dstTs - tmpHdr.origTs;
    tmpHdr.seqNum = ntohl(((int_hdr->seq))); 
    tmpHdr.sink_jitter = tmpHdr.dstTs - meta_tmp.prev_dstTs;
    
    if(tmpHdr.seqNum == 0) {
        tmpHdr.reordering = 0;
        tmpHdr.seqNum = meta_tmp.seq + 1; 
        tmpHdr.protocol = UDP;
    } else {
        tmpHdr.reordering = tmpHdr.seqNum - meta_tmp.seq - 1; 
        tmpHdr.protocol = TCP;
    } 
    
    // Update flow data
    meta_tmp.prev_dstTs = tmpHdr.dstTs;
    meta_tmp.seq = tmpHdr.seqNum;
}

/**
 * Write node information to format sutable for sending
 * \param tmpHdr Where to store parsed information
 * \param int_meta_hdr Raw data from packet
 * \param meta_cnt Number of nodes to proccess
 */
void get_int_node_data(telemetric_hdr_t &tmpHdr, struct int_meta_t *int_meta_hdr, const uint8_t meta_cnt) {
    // Convert source timestamp
    tmpHdr.origTs = ntoh64(((int_meta_hdr->ingress_tstamp)));

    // Storing information for delay counting
    int64_t tmp_eg_timestamp = ntoh64(int_meta_hdr->egress_tstamp);
    
    struct telemetric_meta node;
    for(uint8_t i = 0; i < meta_cnt; ++i) {
        node.hop_index = i;
        node.hop_delay = ntoh64(int_meta_hdr->egress_tstamp) - ntoh64(int_meta_hdr->ingress_tstamp);
        node.link_delay = 0;
        node.hop_timestamp = ntoh64(int_meta_hdr->egress_tstamp);

        // Delay can not be counted for first node (There is no previous timestamp)
        if(i != 0)
        {
            node.link_delay = tmp_eg_timestamp -  ntoh64(int_meta_hdr->ingress_tstamp);
            tmp_eg_timestamp = ntoh64(int_meta_hdr->egress_tstamp);
        }

        node.hop_jitter = ntoh64(int_meta_hdr->ingress_tstamp) - prev_timestamps[i];
        prev_timestamps[i] = ntoh64(int_meta_hdr->ingress_tstamp);

        tmpHdr.node_meta.push_back(node); 
        ++int_meta_hdr;
    }

    node.hop_index = meta_cnt;
    node.hop_delay = 0;
    node.link_delay = tmp_eg_timestamp - tmpHdr.dstTs;
    node.hop_timestamp = tmpHdr.dstTs;
    node.hop_jitter = tmpHdr.dstTs - prev_timestamps[meta_cnt];
    prev_timestamps[meta_cnt] = tmpHdr.dstTs;
    
    tmpHdr.node_meta.push_back(node); 
}

/**
 * Report data to influx
 * \param influxdb Database descriptor
 * \param opt Program parameters
 * \param tmpHdr Data to send
 */
void report_to_influx(IntExporter &exporter, const options_t& opt, telemetric_hdr_t &tmpHdr) {
    if(opt.hostValid && (pkt_cnt % opt.smpl_rate == 0)) {
        uint32_t ret = exporter.sendData(tmpHdr);
        if(ret != EXIT_SUCCESS) {
            //printf("Error during the export to InfluxDB\n");
            //return RET_ERR;
            pkt_drop++;
            if(pkt_drop % 1000 == 0 && pkt_drop != 0) {
                std::cout << "dropped: " << pkt_drop << std::endl;
            }
        }
    }
         
    // Print to console
    if(opt.verbose) {
        print_telemetric(&tmpHdr);
    } 
}

/**
 * Process one received packet based on the program
 * \param pkt Input packet to prs
 * \param influxdb Database descriptor
 * \param opt Program parameters
 * \return RET_OK if everything was fine
 */
uint32_t process_packet(struct ndp_packet& pkt, IntExporter &exporter, const options_t& opt) {
    // Prepare telemetric data into the apropriate structure
    telemetric_hdr_t tmpHdr;
    struct int_influx_t *int_hdr = (struct int_influx_t*)pkt.data;
    struct int_influx_t *tmp = int_hdr;
    struct int_meta_t *int_meta_hdr = (struct int_meta_t *)(++tmp);
    
    uint64_t map_key = *((uint64_t*)(pkt.data));
    get_int_header_data(tmpHdr, int_hdr, map_key);

    uint8_t meta_cnt = int_hdr->meta_len/(int_hdr->hop_meta_len);
    get_int_node_data(tmpHdr, int_meta_hdr, meta_cnt);
 
    // Cut of timestamps to 48 bits
    if(opt.tstmp == 1) {
        uint64_t mask = 0x0000FFFFFFFFFFFF;
        tmpHdr.origTs = tmpHdr.origTs & mask;
        tmpHdr.dstTs = tmpHdr.dstTs & mask;
    }

    report_to_influx(exporter, opt, tmpHdr);

    return RET_OK;
}

/** 
 * Print the help
 * \param prgname Name of program 
 */
void print_help(const char* prgname) {
    printf("%s [-d device] [-c collectorAddress] [-p collectorPort] [-r collectorProtocol]" 
           " [-u username] [-s password] [-b numOfReports] [-l logFile] [-m samplingRate]"
           " [-i buffer_size] [-vtkh]\n", prgname);
    printf("\t* -d = ID of the device (e.g.,0 stands for /dev/nfb0, default is 0).\n");
    printf("\t* -c = Host address of the collector.\n");
    printf("\t* -p = Port of collector.\n");
    printf("\t* -r = Protocol of collector.\n");
    printf("\t* -u = Username of collector.\n");
    printf("\t* -s = Password of collector.\n");
    printf("\t* -b = How many reports send at once (default is 1000).\n"); 
    printf("\t* -l = Error messages will be written to given log file.\n"); 
    printf("\t* -m = Set sampling rate of reporting to database (default is 1).\n"); 
    printf("\t* -i = Number of senders.\n"); 
    printf("\t* -v = Enable the verbose mode for printinf of parsed data.\n"); 
    printf("\t* -t = Enable 48-bit timestamp mode.\n");
    printf("\t* -k = Disable P4 device configuration.\n");
    printf("\t* -h = Prints the help.\n");
}

/**
 * Load rules for flow filter
 * \param file Load from this file
 * \param opt Program parameters
 */
void load_flt(const char *file, options_t *opt) {
    std::ifstream infile(file);
    if (infile.fail()) {
        fprintf(stderr, "Failed to open file \"%s\"\n", file);
        exit(1);
    }
    
    std::string line;
    while (std::getline(infile, line)) {
        std::array<uint8_t, 6> array;
        uint16_t port;
        
        sscanf(line.c_str(), "%" SCNu8 ".%" SCNu8 ".%" SCNu8 ".%" SCNu8 " %" SCNu16, 
            &array[3], &array[2], &array[1], &array[0], &port);
        array[4] = port & 0xff;
        array[5] = (port >> 8) & 0xff;

        opt->ip_flt.push_back(array);
    }
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
    opt->p4cfg = 1;
    opt->smpl_rate = 1;
    opt->raw_buffer = 1; 

    int32_t op;
    char* tmp;
     
    // Parse all parameters
    while((op = getopt(argc, argv, "d:c:p:r:u:s:b:l:m:f:i:vtkh")) != -1) {
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
            
            case 'f':
                // Load flow filter
                load_flt(optarg, opt);
                break;
            
            case 'i':
                // Size of internal raw buffer
                opt->raw_buffer = atoi(optarg);
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
               opt->p4cfg = 0;
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
 * \param opt Program parameters
 * \return RET_OK if everything was fine
 */
int loop_proccess(p4device_t &device, nfb_int_dev_t &nfb, IntExporter &exporter, options_t &opt) {
    uint32_t pkt_rx_ret;
    uint32_t ret_pkt_proc;
    struct ndp_packet packets[NDP_PACKET_BUFF]; 
    
    while(!stop) {
        // Read the packet from the buffer
        pkt_rx_ret = ndp_rx_burst_get(nfb.rx, packets, NDP_PACKET_BUFF);
    
        // flush influxdb buffer
        if(pkt_rx_ret == 0) {
            delay_usecs(100);
            continue;
        }

        // Process all packets 
        for(uint8_t i = 0; i < pkt_rx_ret; i++) {
            // Increment counter for each finished one 
            pkt_cnt++;
            // Process packet
            ret_pkt_proc = process_packet(packets[i], exporter, opt);
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
  
    // Prepare exporter 
    IntExporter exporter(&opt);
    // infinite loop packet processing
    ret = loop_proccess(device, nfb, exporter, opt);
    
    return ret;
}
