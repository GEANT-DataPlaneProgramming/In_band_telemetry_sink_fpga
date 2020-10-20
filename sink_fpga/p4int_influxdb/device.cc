/**
 * @author Mario Kuka <kuka@cesnet.cz>
 *         Pavlina Patova <xpatov00@stud.fit.vutbr.cz>
 * @brief Header file of INT sink node
 *     
 * Copyright (c) 2015 - 2018 CESNET, z.s.p.o.
 */

#include <cstring>
#include <p4dev_base.h>

#include "device.h"

int32_t open_device(p4device_t* device, options_t* opt, nfb_int_dev_t* nfb) {
    // Initialize the input structure
    nfb->dev = NULL;
    nfb->rx  = NULL;

    // Select the right device path
    const char* ndp_dev;
    if(opt->devId == 0) {
        ndp_dev = "/dev/nfb0";
    } else {
        ndp_dev = "/dev/nfb1";
    }

    // Open the NDP and prepare packet transmission
    nfb->dev = nfb_open(ndp_dev);
    if(!nfb->dev) {
        printf("Error during the openning of the NFB device!!\n");
        close_device(device, opt, nfb);
        return RET_ERR;
    }
    
    // Open RX queue
    nfb->rx = ndp_open_rx_queue(nfb->dev, 0);
    if(!nfb->rx) {
        printf("Error during the opennign of the device queue!!\n");
        close_device(device, opt, nfb);
        return RET_ERR;
    }

    // Start transfer
    int32_t ret = ndp_queue_start(nfb->rx);
    if(ret != NDP_OK) {
        printf("Error during the starting of DMA transfer!\n");
        close_device(device, opt, nfb);
        return RET_ERR;
    } 
    
    return RET_OK;
}

void close_device(p4device_t* device, options_t* opt, nfb_int_dev_t* nfb) {
    // Close the device
    if(nfb->rx){
        ndp_queue_stop(nfb->rx);
        ndp_close_rx_queue(nfb->rx);
    }
    nfb->rx = NULL;

    if(nfb->dev) { 
        nfb_close(nfb->dev);
    }
    nfb->dev = NULL;
}

/**
 * Configre p4 table "table_eth_map"
 * \param device Device structure
 * \param opt Options of the device tree.
 * \return \ref RET_OK on success
 */
const int32_t eth_map(const p4device_t* device, const options_t* opt) {
    uint32_t xret;
    p4table_t *table;
    p4rule_t *rule;

    // Prepare the rule -- prepare the default one
    table = p4device_get_table(device, "table_eth_map");
    if (table == NULL) {
        return RET_ERR;
    }
    
    rule = p4table_get_rule_template(table);
    p4rule_add_action(rule, "NoAction");
    p4rule_mark_default(rule);
    
    xret = p4table_insert_default_rule(table, rule);
    if (xret != P4DEV_OK) {
        return RET_ERR;
    }
    printf("P4INT rule for table_eth_map been configured!\n");
    
    p4rule_free(rule); 
    return RET_OK;
}

/**
 * Configre p4 table "table_cha_map"
 * \param device Device structure
 * \param opt Options of the device tree.
 * \return \ref RET_OK on success
 */
int32_t dma_map(const p4device_t* device, const options_t* opt) {
    uint32_t xret;
    p4table_t *table;
    p4rule_t *rule;

    table = p4device_get_table(device, "table_cha_map");
    if (table == NULL) {
        return RET_ERR;
    }
    
    rule = p4table_get_rule_template(table);
    p4rule_add_action(rule, "map_to_chan");
    p4rule_mark_default(rule);
    
    xret = p4table_insert_default_rule(table, rule);
    if (xret != P4DEV_OK) {
        return RET_ERR;
    }
    printf("P4INT rule for map_to_chan been configured!\n");
   
    p4rule_free(rule); 
    return RET_OK;
}

/**
 * Configre p4 table "table_influx"
 * \param device Device structure
 * \param opt Options of the device tree.
 * \return \ref RET_OK on success
 */
int32_t influx_map(const p4device_t* device, const options_t* opt) {
    uint32_t xret;
    p4table_t *table;
    p4rule_t *rule;
    
    table = p4device_get_table(device, "table_influx");
    if (table == NULL) {
        return RET_ERR;
    }
   
    // Configure default rule
    p4table_reset(table, 0);
    rule = p4table_get_rule_template(table);
    p4rule_add_action(rule, "pkt_drop");
    p4rule_mark_default(rule);
    
    xret = p4table_insert_default_rule(table, rule);
    if (xret != P4DEV_OK) {
        return RET_ERR;
    }
    p4rule_free(rule); 
    
    // Configure flow filter
    // TODO: Configure filter from cfg file and program argument
    uint8_t ip[4]   = {196, 169, 254, 150}; // PL    
    uint8_t ip2[4]  = {213, 95, 77, 217};   // IT 
    uint8_t port[2] = {104, 66};  // 17000 

    rule = p4table_get_rule_template(table);
    p4rule_add_key_element(rule, p4key_exact_create("ip.srcAddr", 4, ip));
    p4rule_add_key_element(rule, p4key_exact_create("udp.dprt", 2, port));
    p4rule_add_action(rule, "fill_influx");
    uint32_t index;
    xret = p4table_insert_rule(table, rule, &index, true);   
    p4rule_free(rule); 
    if (xret != P4DEV_OK) {
        return RET_ERR;
    }
    
    rule = p4table_get_rule_template(table);
    p4rule_add_key_element(rule, p4key_exact_create("ip.srcAddr", 4, ip2));
    p4rule_add_key_element(rule, p4key_exact_create("udp.dprt", 2, port));
    p4rule_add_action(rule, "fill_influx");
    xret = p4table_insert_rule(table, rule, &index, true);   
    p4rule_free(rule); 
    if (xret != P4DEV_OK) {
        return RET_ERR;
    }

    printf("P4INT rule for fill_infllux been configured!\n");
    return RET_OK;
}

int32_t configure_device(const p4device_t* devices, const options_t* opt) {
    uint32_t xret;
    p4device_t device;

    // Open the device 
    xret = p4device_init(&device, NULL, 0, P4DEVICE_DEFAULT_COMPONENT);
    if(xret != P4DEV_OK){
        printf("p4dev_direct_init failed!\n");
        printf("* ");
        p4dev_err_stderr(xret); 
        return RET_ERR;
    } 
    
    // Disable the pipeline
    printf("Configuring the P4INT ...\n");
    xret = p4base_disable(&device);
    if(xret != P4DEV_OK) {
        return RET_ERR;
    }
    
    // Configure tables 
    if(eth_map(&device, opt) != RET_OK) {
        return RET_ERR;
    }    
    if(dma_map(&device, opt) != RET_OK) {
        return RET_ERR;
    }    
    if(influx_map(&device, opt) != RET_OK) {
        return RET_ERR;
    }    
   
    // Enable the pipeline
    xret = p4base_enable(&device);
    if(xret != P4DEV_OK) {
        return RET_ERR;
    }
    printf("Network device was enabled!\n");

    // TODO: segfault bug 
    // p4device_free(&device);
    return RET_OK;
}

