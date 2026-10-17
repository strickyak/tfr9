#ifndef FIRMWARE_H_
#define FIRMWARE_H_

#ifndef FUZIX_COCO2CENTIPEDE
#define FUZIX_COCO2CENTIPEDE 1
#endif

struct CentipedeConfig {
    bool    ram_64k;
    bool    rom_disk11;
    bool    floppy_fd;
    bool    floppy_pc;
    bool    trace_writes;
    bool    trace_reads;
    bool    become_coco3;
#if FUZIX_COCO2CENTIPEDE
    bool    fuzix;
#endif

    void SetStandard() {
        this->ram_64k = true;
        this->rom_disk11 = true;
        this->floppy_fd = true;
        this->trace_writes = false;
        this->trace_reads = false;

        this->floppy_pc = false;
        this->become_coco3 = false;
#if FUZIX_COCO2CENTIPEDE
        this->fuzix = false;
#endif
    }
};

CentipedeConfig centipede_config;

#endif //// FIRMWARE_H_
