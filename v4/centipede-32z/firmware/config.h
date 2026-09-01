#ifndef FIRMWARE_H_
#define FIRMWARE_H_

struct CentipedeConfig {
    bool    ram_64k;
    bool    rom_disk11;
    bool    floppy_fd;
    bool    floppy_pc;
    bool    trace_writes;
    bool    trace_reads;

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
