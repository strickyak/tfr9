#ifndef FIRMWARE_H_
#define FIRMWARE_H_

struct CentipedeConfig {
    bool    ram_64k;
    bool    rom_disk11;
    bool    floppy_fd;
    bool    floppy_pc;
    bool    trace_writes;
    bool    trace_reads;
    bool    become_coco3;
    bool    fujinet;  // becker port at $FF41/$FF42 -> UART, and HDB-DOS at $C000
    bool    trace_becker;  // log becker-port bytes to the tether window

    void SetStandard() {
        this->ram_64k = true;
        this->rom_disk11 = true;
        this->floppy_fd = true;
        this->trace_writes = false;
        this->trace_reads = false;

        this->floppy_pc = false;
        this->become_coco3 = false;
        this->fujinet = false;
        this->trace_becker = false;
    }
};

CentipedeConfig centipede_config;

#endif //// FIRMWARE_H_
