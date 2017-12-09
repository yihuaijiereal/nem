
#pragma once

#include <semaphore.h>
#include <sys/queue.h>
#include <vector>
#include <infiniband/verbs_exp.h>
#include "MemoryRegion.hpp"

//struct ibv_context;
//struct ibv_pd;
//struct ibv_exp_ec_calc;
//struct ibv_exp_ec_calc_init_attr;
//struct ibv_exp_ec_comp;
//struct ibv_exp_ec_mem;
//struct ibv_mr;
struct ibv_sge;

namespace rdma {
    
    class Network;
    class MemoryRegion;

    struct ec_comp;

    class ECCalc {
        friend void put_ec_comp(ECCalc &calc, struct ec_comp *comp);

        friend struct ec_comp *get_ec_comp(ECCalc &calc);

    private:
        
        int                                 k;
        int                                 m;
        int                                 w;

        struct ibv_exp_ec_calc              *calc;
        struct ibv_exp_ec_calc_init_attr    attr;
        
        uint8_t                             *en_mat;
        //uint8_t                             *de_mat;
        int                                 *encode_matrix;
        int                                 block_size;
        struct ec_comp                      *comp;
        SLIST_HEAD(, ec_comp)               comps_list;

        sem_t                               sem;
        pthread_spinlock_t                  lock;

        Network                             &network;

        int alloc_encode_matrix(int k, int m, int w, uint8_t **en_mat, int **encode_matrix);

        int alloc_comp_ec_mrs(struct ec_comp *comp);

        void free_comp_ec_mrs(struct ec_comp *comp);

        void print_matrix_u8(uint8_t *m, int rows, int cols);

        int alloc_decode_matrix();

        int extract_erasures(char *failed_blocks, struct ec_comp *comp);

        int alloc_decode_matrix(struct ec_comp *comp);

        //void free_decode_matrix(struct ec_comp *comp);
        //void ec_encode_done(struct ibv_exp_ec_comp *ib_comp);

        //void ec_decode_done(struct ibv_exp_ec_comp *ib_comp);


    public:
        ECCalc(Network &network, int k, int m, int w, int frame_size);

        ~ECCalc();

        int encode(std::vector<MemoryRegion::Slice> &data, std::vector<MemoryRegion::Slice>  &code, 
            void (*ec_done)(struct ibv_exp_ec_comp *ib_comp));
        
        int decode(std::vector<MemoryRegion::Slice> &data, std::vector<MemoryRegion::Slice>  &code, 
            char *failed_blocks, void (*ec_done)(struct ibv_exp_ec_comp *ib_comp));

        int encode(std::vector<MemoryRegion::Slice> &data, std::vector<MemoryRegion::Slice>  &code);
        
        int decode(std::vector<MemoryRegion::Slice> &data, std::vector<MemoryRegion::Slice>  &code, 
            char *failed_blocks);
    };

}
