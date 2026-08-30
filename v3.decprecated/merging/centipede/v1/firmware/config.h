#ifndef FIRMWARE_H_
#define FIRMWARE_H_

struct CentipedeConfig {
    volatile bool    ram_64k;
    volatile bool    rom_disk11;
    volatile bool    floppy_fd;
    volatile bool    floppy_pc;
    volatile bool    trace_writes;
    volatile bool    trace_reads;

    void SetAll(bool b) {
        this->ram_64k = b;
        this->rom_disk11 = b;
        this->floppy_fd = b;
        this->floppy_pc = b;
        this->trace_writes = b;
        this->trace_reads = b;
    }
};

CentipedeConfig centipede_config;

#endif //// FIRMWARE_H_
