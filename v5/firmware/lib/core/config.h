#ifndef FIRMWARE_H_
#define FIRMWARE_H_

struct CentipedeConfig {
    volatile bool    ram_64k;
    volatile bool    rom_disk11;
    volatile bool    floppy_fd;
    volatile bool    floppy_pc;
    volatile bool    trace_writes;
    volatile bool    trace_reads;

    char             engine[32];
    char             flat_img[64];
    char             drive0[64];
    char             drive1[64];
    char             drive2[64];
    char             drive3[64];
    char             task1[64];
    char             task2[64];

    void SetAll(bool b) {
        this->ram_64k = b;
        this->rom_disk11 = b;
        this->floppy_fd = b;
        this->floppy_pc = b;
        this->trace_writes = b;
        this->trace_reads = b;
        this->engine[0] = 0;
        this->flat_img[0] = 0;
        this->drive0[0] = 0;
        this->drive1[0] = 0;
        this->drive2[0] = 0;
        this->drive3[0] = 0;
        this->task1[0] = 0;
        this->task2[0] = 0;
    }
};

CentipedeConfig centipede_config;

#endif //// FIRMWARE_H_
