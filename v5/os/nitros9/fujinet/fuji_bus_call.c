/**
 * fuji_bus_call.c -- NitrOS-9 / CMOC varargs FujiBus dispatcher for fujinet-lib v5.0.1.
 *
 * Replaces the git-ignored fuji_bus_call.s from upstream nitros9-l2/fujinet/fujinet-lib/bus/os9/,
 * routing OP_FUJI (0xE2), OP_NET (0xE3), and OP_CLOCK (0xE5) transactions over scdwv's SS.Fuji
 * atomic SetStat via ssfuji.c / dwwrite() / dwread().
 */

#include <cmoc.h>
#include <stdarg.h>
#include "dw.h"
#include "ssfuji.h"
#include "fujinet-fuji-coco.h"
#include "fujinet-network-coco.h"
#include <fujinet-int.h>
#include <fujinet-bus.h>
#include <fujinet-err.h>

bool fuji_bus_call(uint8_t device, uint8_t fuji_cmd, uint8_t fields, ...)
{
    va_list ap;
    uint8_t aux[4];
    uint8_t hdr[3];
    uint8_t n_aux;
    uint8_t send_aux;
    uint8_t i;
    void *data_ptr = NULL;
    size_t data_len = 0;
    void *reply_ptr = NULL;
    size_t reply_len = 0;

    aux[0] = 0;
    aux[1] = 0;
    aux[2] = 0;
    aux[3] = 0;

    n_aux = fuji_field_numbytes(fields);

    va_start(ap, fields);
    for (i = 0; i < n_aux; i++) {
        aux[i] = (uint8_t) va_arg(ap, unsigned int);
    }
    if (fields & FUJI_FIELD_DATA) {
        data_ptr = va_arg(ap, void *);
        data_len = va_arg(ap, size_t);
    }
    if (fields & FUJI_FIELD_REPLY) {
        reply_ptr = va_arg(ap, void *);
        reply_len = va_arg(ap, size_t);
    }
    va_end(ap);

    bus_ready();

    if (device == FUJI_DEVICEID_FUJINET) {
        hdr[0] = OP_FUJI;
        hdr[1] = fuji_cmd;
        dwwrite(hdr, 2);
        if (n_aux > 0) {
            dwwrite(aux, (int) n_aux);
        }
        if ((fields & FUJI_FIELD_DATA) && data_len > 0) {
            dwwrite((byte *) data_ptr, (int) data_len);
        }
        if (fuji_get_error()) {
            return false;
        }
        if ((fields & FUJI_FIELD_REPLY) && reply_len > 0) {
            if (!fuji_get_response((uint8_t *) reply_ptr, (int) reply_len)) {
                return false;
            }
        }
        return true;
    }

    if (device >= FUJI_DEVICEID_NETWORK && device <= FUJI_DEVICEID_NETWORK_LAST) {
        uint8_t unit = device - FUJI_DEVICEID_NETWORK + 1;
        hdr[0] = OP_NET;
        hdr[1] = unit;
        hdr[2] = fuji_cmd;
        dwwrite(hdr, 3);
        /* On CoCo DriveWire, all OP_NET packets carry at least 2 aux bytes. */
        send_aux = (n_aux < 2) ? 2 : n_aux;
        dwwrite(aux, (int) send_aux);
        if ((fields & FUJI_FIELD_DATA) && data_len > 0) {
            dwwrite((byte *) data_ptr, (int) data_len);
        }
        if (network_get_error(unit) != FN_ERR_OK) {
            return false;
        }
        if ((fields & FUJI_FIELD_REPLY) && reply_len > 0) {
            if (network_get_response(unit, (uint8_t *) reply_ptr, (int) reply_len) != FN_ERR_OK) {
                return false;
            }
        }
        return true;
    }

    if (device == FUJI_DEVICEID_CLOCK) {
        hdr[0] = OP_CLOCK;
        hdr[1] = fuji_cmd;
        dwwrite(hdr, 2);
        if (n_aux > 0) {
            dwwrite(aux, (int) n_aux);
        }
        if ((fields & FUJI_FIELD_DATA) && data_len > 0) {
            dwwrite((byte *) data_ptr, (int) data_len);
        }
        if ((fields & FUJI_FIELD_REPLY) && reply_len > 0) {
            return dwread((byte *) reply_ptr, (int) reply_len) != 0;
        }
        return ssfuji_flush() != 0;
    }

    return false;
}
