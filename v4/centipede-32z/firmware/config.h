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

    void SetStandard() {
        this->ram_64k = true;
        this->rom_disk11 = true;
        this->floppy_fd = true;
        this->trace_writes = true;
        this->trace_reads = true;

        this->floppy_pc = false;
        this->become_coco3 = false;
    }
};

CentipedeConfig centipede_config;

#endif //// FIRMWARE_H_
