/**
 * @author Mario Kuka <kuka@cesnet.cz>
 *         Pavlina Patova <xpatov00@stud.fit.vutbr.cz>
 * @brief Header file of INT sink node
 * 
 * Copyright (c) 2015 - 2018 CESNET, z.s.p.o.
 */

#include <stdint.h>
#include <p4dev.h>
#include <nfb/ndp.h>
#include <nfb/nfb.h>

#include "p4int.h"

#ifndef _DEVICE_H_
#define _DEVICE_H_

// Bitmap for RX
#define RX_BITMAP 0xFF
// Bitmap for TX 
#define TX_BITMAP 0x00

/**
 * Sructure with all data related to the configuration of the nfb device
 */
typedef struct {
    struct nfb_device* dev; // NFB device we are working with 
    ndp_rx_queue_t*    rx;  // Opened RX queue
} nfb_int_dev_t;


/** 
 * Open the device 
 * \param device Device structure
 * \param opt Options of the device tree.
 * \param nfb Strcture with information about the device and RX queue
 * \return \ref RET_OK on success
 */
int32_t open_device(p4device_t* device, options_t* opt, nfb_int_dev_t* nfb);

/** 
 * Free the device
 * \param device Device structure
 * \param opt Options of the device tree.
 * \param nfb Strcture with information about the device and RX queue
 */
void close_device(p4device_t* device, options_t* opt, nfb_int_dev_t* nfb);

/**
 * Configure the device including the sampling ratio 
 * \param device Device structure
 * \param opt Options of the device
 * \return \ref RET_OK on success
 */
int32_t configure_device(const p4device_t* devices, const options_t* opt);

#endif // _DEVICE_H_
