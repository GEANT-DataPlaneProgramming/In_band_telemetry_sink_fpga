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

#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <linux/if_packet.h>
#include <net/ethernet.h>
#include "ipk-print.h"
    
#include "device.h"
#include "p4int.h"
#include "p4_influxdb.h"

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

    // Convert source timestamp
    tmpHdr.origTs = ntoh64(((int_meta_hdr->ingress_tstamp)));

    // Convert destination timestamp
    tmpHdr.dstTs = ntohl(((int_hdr->ndk_tstamp2)));
    tmpHdr.dstTs += ntohl(((int_hdr->ndk_tstamp1)))*  1'000'000'000ll;

    // Cut of timestamps to 48 bits
    if(opt.tstmp == 1) {
        uint64_t mask = 0x0000FFFFFFFFFFFF;
        tmpHdr.origTs = tmpHdr.origTs & mask;
        tmpHdr.dstTs = tmpHdr.dstTs & mask;
    }

    // Convert IP addresses 
    inet_ntop(AF_INET, &int_hdr->srcAddr, tmpHdr.srcIp, IP_BUFF_SIZE);
    inet_ntop(AF_INET, &int_hdr->dstAddr, tmpHdr.dstIp, IP_BUFF_SIZE);

    // Convert source and destination ports
    tmpHdr.srcPort =  ntohs(((int_hdr->ingress_port_id)));
    tmpHdr.dstPort =  ntohs(((int_hdr->egress_port_id)));

    uint8_t meta_len = (((int_hdr->meta_len))); 

    // Get flow data
    // TODO: Implement flow hash table 
    uint64_t map_key = *((uint64_t*)(pkt.data));
    meta_data &meta_tmp = flow_map[map_key];
   
    // Calculate int header
    tmpHdr.delay = tmpHdr.dstTs - tmpHdr.origTs;
    tmpHdr.seqNum = ntohl(((int_hdr->seq))); 
    tmpHdr.sink_jitter = tmpHdr.dstTs - meta_tmp.prev_dstTs;
    
    if(meta_tmp.seq == 0) {
        tmpHdr.reordering = 0; 
    } else {
        tmpHdr.reordering = tmpHdr.seqNum - meta_tmp.seq - 1; 
    } 
    
    // Update flow data
    meta_tmp.prev_dstTs = tmpHdr.dstTs;
    meta_tmp.seq = tmpHdr.seqNum;

    // Report to influxdb
    if(opt.hostValid && (pkt_cnt % opt.smpl_rate == 0)) {
        uint32_t ret = exporter.sendData(tmpHdr);
        if(ret != EXIT_SUCCESS) {
            //printf("Error during the export to InfluxDB\n");
            //return RET_ERR;
            pkt_drop++;
            if(pkt_drop % 1000 == 0 && pkt_drop != 0) {
                std::cout << "dopped: " << pkt_drop << std::endl;
            }
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
    printf("\t* -e = Interface designed to catch traffic.\n");
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
    strcpy(opt->interface,"lo");

    int32_t op;
    char* tmp;
     
    // Parse all parameters
    while((op = getopt(argc, argv, "d:c:p:r:u:s:b:l:m:f:i:vtkhe:")) != -1) {
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

            case 'e':
                memset(opt->interface, 0, CHAR_BUFF_SIZE);
                strcpy(opt->interface, optarg);
                break;

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
// int loop_proccess(int &sockfd, IntExporter &exporter, options_t &opt) {
    int loop_proccess(int &sockfd, options_t &opt) {
    printf("In loop\n");
    uint32_t size;
    uint32_t ret_pkt_proc;
    int BUF_SIZ = 1024; // TODO move to the .h file
    unsigned char *packet = (unsigned char*)malloc(sizeof(char)*BUF_SIZ); 
    if(packet == nullptr) {
        fprintf(stderr, "P4INT>ERROR: Cannot alocate memory\n");
        close(sockfd);
        return RET_ERR;
    }
    
    
    while(!stop) {
        // Read the packet from the buffer
        printf("Preparing Transmition\n");
        memset(packet, 0, BUF_SIZ);

        // TODO if it returns 0 socket is closed -> we need to open it again

        // NOTE doesnt seem that options do something... still receving all packets
        // struct sockaddr *src_addr;
        // struct sockaddr_in addr;
        // addr.sin_family = AF_INET;
        // addr.sin_port = htons(17000);
        // addr.sin_addr.s_addr = inet_addr("150.254.169.196.4607");
        // // addr.sin_addr.s_addr = htonl(INADDR_ANY);
        // socklen_t addr_len = sizeof(struct sockaddr_in);
        // //size = recvfrom(sockfd, packet, BUF_SIZ, 0, (struct sockaddr*)&addr, &addr_len);
        
        size = recvfrom(sockfd, packet, BUF_SIZ, 0, NULL, NULL);


        printf("Paket recieved\n");
        print_data(packet, size);

        // flush influxdb buffer
        if(size == 0) {
            delay_usecs(100);
            fprintf(stderr, "Zero recieved\n");
            continue;
        }
        else if(size < 0) {
            fprintf(stderr, "P4INt>ERROR: Cannot recieve packet\n");
            return RET_ERR;
        }

        //ret_pkt_proc = process_packet(packets[i], exporter, opt);
        if(ret_pkt_proc != RET_OK) {
            fprintf(stderr, "Error during the packet processing!\n");
            free(packet);
            close(sockfd);
            return RET_ERR;
        }

        // // Process all packets 
        // for(uint8_t i = 0; i < size; i++) {
        //     // Increment counter for each finished one 
        //     pkt_cnt++;
        //     // Process packet
        //     ret_pkt_proc = process_packet(packets[i], exporter, opt);
        //     if(ret_pkt_proc != RET_OK) {
        //         printf("Error during the packet processing!\n");
        //         close_device(&device, &opt, &nfb);
        //         return RET_ERR;
        //     }
        // }
    }
 
    free(packet);
    close(sockfd);
    return RET_OK;
}


// int32_t socket_set(const options_t opt, struct ifreq *ifreq, int sockfd, struct sockaddr_ll *saddr){
int32_t socket_set(const options_t &opt, int &sockfd){

    /* Open PF_PACKET socket, listening for EtherType ETHER_TYPE */

    // NOTES
    // socket(domain, type, protocol)
    // zero means any protocol
    // IPPROTO_RAW - for raw socket
    // AF_INET - Error ocures
	if ((sockfd = socket(AF_PACKET, SOCK_RAW, 0)) == -1) {
		fprintf(stderr, "P4INT>ERROR: Cannot create socket. Try run with sudo\n");
		return RET_ERR;
	}
	/* Allow the socket to be reused - incase connection is closed prematurely */
	// if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &sockopt, sizeof sockopt) == -1) {
	// 	fprintf(stderr, "P4INT>ERROR: Setsockopt failed\n");
	// 	close(sockfd);
	// 	return RET_ERR;
	// }
	/* Bind to device */


    struct ifreq ifc;
    snprintf(ifc.ifr_name, sizeof(ifc.ifr_name), opt.interface);

    struct sockaddr_ll saddr;
    
    if (ioctl(sockfd, SIOCGIFINDEX, &ifc)){
        fprintf(stderr, "P4INT>ERROR: Ioctl failed\n");
        return RET_ERR;
    }
    //memset(saddr, 0, sizeof(saddr));
    saddr.sll_family = AF_PACKET;
    saddr.sll_protocol = htons(ETH_P_ALL);
    saddr.sll_ifindex = ifc.ifr_ifindex;
    saddr.sll_pkttype = PACKET_HOST;

    if(bind(sockfd, (struct sockaddr *)&saddr, sizeof(saddr)) == -1){
        fprintf(stderr, "Cannot bind socket to interface %s\n", opt.interface);
        return RET_ERR;
    }

    //NOTE
    // Can not quit using CTRL+C
	// if (setsockopt(sockfd, SOL_SOCKET, SO_BINDTODEVICE, (char*)&ifc, sizeof(ifc)) == -1)	{
    //     fprintf(stderr, "Cannot bind socket to interface %s\n", opt.interface);
	// 	close(sockfd);
	// 	return RET_ERR;
	// }
    return RET_OK;
}

int32_t main(int32_t argc, char** argv) {
    // Prepare the configuration
    int32_t ret;
    options_t opt;
    if(parse_arguments(&opt, argc, argv) != RET_OK) {
        return RET_ERR;
    }

    // Register signal to enable catching of Ctrl+c
    if(signal(SIGINT, setup_stop) == SIG_ERR) {
        printf("Unable to register SIGINT handler!\n");
        return RET_ERR;
    }
  
    int sockfd;
    ret = socket_set(opt, sockfd);
    if(ret != RET_OK) {
        return RET_ERR;
    }
    printf("Socket created\n");

    // Prepare exporter 
    //IntExporter exporter(&opt);

    // infinite loop packet processing
   // ret = loop_proccess(sockfd, exporter, opt);
   ret = loop_proccess(sockfd, opt);
    
    return ret;
}
