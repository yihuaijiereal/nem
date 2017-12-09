
#include <jerasure.h>
#include <jerasure/reed_sol.h>
#include <cstring>
#include <iostream>
#include <string>
#include <gf_complete.h>
#include "Network.hpp"
#include "head.hpp"
#include "ECCalc.hpp"
#include <unistd.h>

using namespace std;

namespace rdma {
    static const int max_inflight_calcs = 8;
    static const int affinity = 0;

    struct ec_comp {
        ECCalc                   *calc;
        struct ibv_exp_ec_comp   comp;
        struct ibv_exp_ec_mem    mem;
        struct ibv_sge           *data;
        struct ibv_sge           *code;
        unsigned long long       bytes;
        int                      index;
        pthread_mutex_t          mutex;
        pthread_cond_t           cond;
        uint8_t                  *de_mat;
        uint8_t                  *erasures;
        int                      *erasures_arr;
        int                      *survived_arr;
        uint8_t                  *survived;
        SLIST_ENTRY(ec_comp)     entry;
    };

    //----------------------------------------------------------------------
    void put_ec_comp(ECCalc &calc, struct ec_comp *comp){
        pthread_spin_lock(&calc.lock);
        SLIST_INSERT_HEAD(&calc.comps_list, comp, entry);
        pthread_spin_unlock(&calc.lock);
        sem_post(&calc.sem);
        //cerr<<"successfully put one comp0000000000000000000000000000000"<<endl;
    }

    //----------------------------------------------------------------------
    struct ec_comp* get_ec_comp(ECCalc &calc){
        struct ec_comp *comp;
        sem_wait(&calc.sem);
        pthread_spin_lock(&calc.lock);
        comp = SLIST_FIRST(&calc.comps_list);
        SLIST_REMOVE(&calc.comps_list, comp, ec_comp, entry);
        pthread_spin_unlock(&calc.lock);
        //cerr<<"successfully get one comp0000000000000000000000000000000"<<endl;
        return comp;
    }

    //-------------------------------------------------------------------
    void ec_encode_done(struct ibv_exp_ec_comp *ib_comp){
        struct ec_comp *comp = (struct ec_comp *)((void *)ib_comp - offsetof(struct ec_comp, comp));
        //encode succeed operation
        //cerr<<"successfully encoded one vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv"<<endl;
        pthread_mutex_lock(&comp->mutex);
        pthread_cond_signal(&comp->cond);
        pthread_mutex_unlock(&comp->mutex);
        put_ec_comp(*comp->calc, comp);

        //sem_post(&sem);
    }

    void ec_decode_done(struct ibv_exp_ec_comp *ib_comp){
        struct ec_comp *comp = (struct ec_comp *)((void *)ib_comp - offsetof(struct ec_comp, comp));
        //decode succeed operation

        //cerr<<"successfully decoded one vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv"<<endl;
        pthread_mutex_lock(&comp->mutex);
        pthread_cond_signal(&comp->cond);
        pthread_mutex_unlock(&comp->mutex);
        put_ec_comp(*comp->calc, comp);

        //sem_post(&sem);
    }

    //----------------------------------------------------------------------
    ECCalc::ECCalc(Network &network, int k, int m, int w, int frame_size)
        :network(network), k(k), m(m), w(w)
    {
        //printf("context addr in ECCalc : %lld\n",network.context);
        struct ibv_exp_device_attr dattr;
        int err, i;
        memset(&dattr, 0, sizeof(dattr));
        dattr.comp_mask = IBV_EXP_DEVICE_ATTR_EXP_CAP_FLAGS | IBV_EXP_DEVICE_ATTR_EC_CAPS;
        err = ibv_exp_query_device(network.context, &dattr);
        if(err){
            string reason = "Couldn't query device for EC offload caps " + to_string(errno) + ": " + strerror(errno);
            cerr << reason << endl;
            throw NetworkException(reason);
        }

        if (!(dattr.exp_device_cap_flags & IBV_EXP_DEVICE_EC_OFFLOAD)) {
            string reason = "EC offload not supported by driver.";
            cerr << reason << endl;
            throw NetworkException(reason);
        }

        attr.comp_mask = IBV_EXP_EC_CALC_ATTR_MAX_INFLIGHT |
            IBV_EXP_EC_CALC_ATTR_K |
            IBV_EXP_EC_CALC_ATTR_M |
            IBV_EXP_EC_CALC_ATTR_W |
            IBV_EXP_EC_CALC_ATTR_MAX_DATA_SGE |
            IBV_EXP_EC_CALC_ATTR_MAX_CODE_SGE |
            IBV_EXP_EC_CALC_ATTR_ENCODE_MAT |
            IBV_EXP_EC_CALC_ATTR_AFFINITY |
            IBV_EXP_EC_CALC_ATTR_POLLING;
        
        attr.max_inflight_calcs = max_inflight_calcs;
        attr.k = k;
        attr.m = m;
        attr.w = w;
        attr.max_data_sge = k;
        attr.max_code_sge = m;
        attr.affinity_hint = affinity;
        block_size = align_any((frame_size + attr.k - 1) / attr.k, 64);
        //cout<<"BLOCK SIZE : "<<block_size<<endl;

        
        comp = (struct ec_comp*)calloc(max_inflight_calcs, sizeof(*comp));
        if (!comp) {
            string reason = "Failed to allocate EC context comps " + to_string(errno) + ": " + strerror(errno);
            cerr << reason << endl;
            throw NetworkException(reason);
        }

        sem_init(&sem, 0, max_inflight_calcs);
        pthread_spin_init(&lock, PTHREAD_PROCESS_PRIVATE);
        SLIST_INIT(&comps_list);

        for (i = 0 ; i < max_inflight_calcs ; i++) {
            comp[i].mutex = PTHREAD_MUTEX_INITIALIZER;
            comp[i].cond = PTHREAD_COND_INITIALIZER;
            comp[i].index = i;
            comp[i].calc = this;
            //comp[i].ctx = ctx;
            err = alloc_comp_ec_mrs(&comp[i]);
            if (err)
                //goto free_mrs;
            put_ec_comp(*this, &comp[i]);
        }
        

        err = alloc_encode_matrix(attr.k, attr.m, attr.w,
                  &en_mat, &encode_matrix);
        if (err)
        {
            string reason = "can't allocate encode matrix.";
            cerr << reason << endl;
            throw NetworkException(reason);
        }
        
        attr.encode_matrix = en_mat;

        calc = ibv_exp_alloc_ec_calc(network.protectionDomain, &attr);
        if (!calc) {
            string reason = "Failed to allocate EC calc.";
            cerr << reason << endl;
            throw NetworkException(reason);
        }

    }

    //----------------------------------------------------------------------
    ECCalc::~ECCalc(){
        unsigned i;

        ibv_exp_dealloc_ec_calc(calc);
        free(encode_matrix);
        free(attr.encode_matrix);
        
        for (i = 0 ; i < attr.max_inflight_calcs ; i++) {
            put_ec_comp(*this, &comp[i]);
            free_comp_ec_mrs(&comp[i]);
        }

        free(comp);  
    }

    //----------------------------------------------------------------------
    int ECCalc::alloc_encode_matrix(int k, int m, int w, uint8_t **en_mat, int **encode_matrix)
    {
        uint8_t *matrix;
        int *rs_mat;
        int i, j;

        matrix = (uint8_t *)calloc(1, m * k);
        if (!matrix) {
            return -ENOMEM;
        }

        rs_mat = reed_sol_vandermonde_coding_matrix(k, m, w);
        if (!rs_mat) {
            return -EINVAL;
        }

        for (i = 0; i < m; i++)
            for (j = 0; j < k; j++)
                matrix[j*m+i] = (uint8_t)rs_mat[i*k+j];
        //print_matrix_u8(matrix, k, m);

        *en_mat = matrix;
        *encode_matrix = rs_mat;

        return 0;
    }

    //----------------------------------------------------------------------
    int ECCalc::alloc_comp_ec_mrs(struct ec_comp *comp){
        comp->erasures_arr = (int *)calloc(k + m, sizeof(int));
        if (!comp->erasures_arr) {
            cerr<<"failed to allocated erasures_arr buffer"<<endl;
            return -ENOMEM;
        }

        comp->erasures = (uint8_t *)calloc(k + m, sizeof(uint8_t));
        if (!comp->erasures) {
            cerr<<"failed to allocated erasures buffer"<<endl;
           return -ENOMEM;
        }

        comp->survived_arr = (int *)calloc(k + m, sizeof(int));
        if (!comp->survived_arr) {
            cerr<<"failed to allocated survived_arr buffer"<<endl;
            return -ENOMEM;
        }

        comp->survived = (uint8_t *)calloc(k + m, sizeof(uint8_t));
        if (!comp->survived) {
            cerr<<"failed to allocated survived buffer"<<endl;
            return -ENOMEM;
        }

        comp->de_mat = (uint8_t *)calloc(m * k, 1);
        if (!comp->de_mat) {
            cerr<< "Failed to allocate decode matrix"<<endl;
            return -ENOMEM;
        }

        comp->data = (struct ibv_sge*)calloc(k,sizeof(*comp->data));
        if (!comp->data) {
            string reason = "Failed to allocate data sges.";
            cerr << reason << endl;
            throw NetworkException(reason);
        }

        comp->code = (struct ibv_sge*)calloc(m,sizeof(*comp->code));
        if (!comp->code) {
            string reason = "Failed to allocate code sges.";
            cerr << reason << endl;
            throw NetworkException(reason);
        }

        comp->mem.data_blocks = comp->data;
        comp->mem.num_data_sge = k;
        comp->mem.code_blocks = comp->code;
        comp->mem.num_code_sge = m;
        comp->mem.block_size = block_size;
    }

    //----------------------------------------------------------------------
    void ECCalc::free_comp_ec_mrs(struct ec_comp *comp){
        free(comp->data);
        free(comp->code);
    }
    /*
    static void print_block_buffer(MemoryRegion::Slice &slice){
        uint8_t *temp;
        temp = (uint8_t *)slice.address;
        for(int i=0;i<slice.size;++i){
            printf("%c",(char)*(temp+i));
        }
        cout<<endl<<"-------------------------------------"<<endl;
    }
    */

    //----------------------------------------------------------------------
    int ECCalc::encode(std::vector<MemoryRegion::Slice> &data, std::vector<MemoryRegion::Slice>  &code, 
        void (*ec_done)(struct ibv_exp_ec_comp *ib_comp))
    {
        int err = 0;
        int i;
        struct ec_comp *comp;
        //cerr<<"in calc encode "<<data.size()<<" "<<code.size()<<endl;

        
        comp = get_ec_comp(*this);
        comp->comp.done = ec_done;

        for(i=0;i<k;++i){
            comp->data[i].lkey = data[i].lkey;
            comp->data[i].addr = (uintptr_t)data[i].address;
            comp->data[i].length = data[i].size;
            //cerr<<"data block addr "<<(uintptr_t)data[i].address <<" ,block size"<< data[i].size<<endl;
        }

        for(i=0;i<m;++i){
            comp->code[i].lkey = code[i].lkey;
            comp->code[i].addr = (uintptr_t)code[i].address;
            comp->code[i].length = code[i].size;
            //cerr<<"parity block addr "<<(uintptr_t)code[i].address <<" ,block size"<< code[i].size<<endl;
        }
        //使用同步的方式编码
        pthread_mutex_lock(&comp->mutex);
        err = ibv_exp_ec_encode_async(calc, &comp->mem, &comp->comp);
        if (err) {
            put_ec_comp(*this, comp);
            return -1;
        }
        pthread_cond_wait(&comp->cond, &comp->mutex);
        pthread_mutex_unlock(&comp->mutex);
        

        return 0;
    }

    //----------------------------------------------------------------------
    int ECCalc::decode(std::vector<MemoryRegion::Slice> &data, std::vector<MemoryRegion::Slice>  &code, 
            char *failed_blocks, void (*ec_done)(struct ibv_exp_ec_comp *ib_comp))
    {
        int err = 0;
        int i;
        struct ec_comp *comp;

        comp = get_ec_comp(*this);
        comp->comp.done = ec_done;

        for(i=0;i<k;++i){
            comp->data[i].lkey = data[i].lkey;
            comp->data[i].addr = (uintptr_t)data[i].address;
            comp->data[i].length = data[i].size;
        }

        for(i=0;i<m;++i){
            comp->code[i].lkey = code[i].lkey;
            comp->code[i].addr = (uintptr_t)code[i].address;
            comp->code[i].length = code[i].size;
        }

        err = extract_erasures(failed_blocks,comp);
        if(err){
            put_ec_comp(*this,comp);
            return -1;
        }

        err = alloc_decode_matrix(comp);
        if(err){
            put_ec_comp(*this,comp);
            return -1;
        }

        pthread_mutex_lock(&comp->mutex);
        err = ibv_exp_ec_decode_async(calc, &comp->mem, 
            comp->erasures, comp->de_mat, &comp->comp);
        if (err) {
            put_ec_comp(*this,comp);
            return -1;
        }
        pthread_cond_wait(&comp->cond, &comp->mutex);
        pthread_mutex_unlock(&comp->mutex);

        return 0;
    }

    //---------------------------------------------------------------------
    int ECCalc::encode(std::vector<MemoryRegion::Slice> &data, std::vector<MemoryRegion::Slice>  &code){
        return encode(data, code, ec_encode_done);
    }
        
    int ECCalc::decode(std::vector<MemoryRegion::Slice> &data, std::vector<MemoryRegion::Slice>  &code, 
        char *failed_blocks){
        return decode(data, code, failed_blocks, ec_decode_done);
    }
    

    //----------------------------------------------------------------------
    void ECCalc::print_matrix_u8(uint8_t *m, int rows, int cols)
    {
        int i, j;

        for (i = 0; i < rows; i++) {
            for (j = 0; j < cols; j++) {
                if (j != 0)
                    printf(" ");

                printf("%#x  ", m[i*cols+j]);
            }
            printf("\n");
        }
    }

    //----------------------------------------------------------------------
    int ECCalc::alloc_decode_matrix(struct ec_comp *comp){
        int *dec_mat;
        int err;
        int i, j, m_e = 0, l = 0; 

        for (i = 0; i < k+m; i++) {
            if (comp->erasures_arr[i])
                m_e++;
        }

        dec_mat = (int *)calloc(k * k, sizeof(int));
        if (!dec_mat) {
            cerr<<"Failed to allocate dec_mat"<<endl;
            return -ENOMEM;
        }

        err = jerasure_make_decoding_matrix(k, m_e, w, encode_matrix,
                        comp->erasures_arr,
                        dec_mat, comp->survived_arr);
        if (err) {
            cerr<<"failed making decoding matrix"<<endl;
            return err;
        }

        for (i = 0; i < k+m; i++) {
            if (comp->erasures_arr[i]) {
                for (j = 0; j < k; j++)
                    comp->de_mat[j*m+l] = (uint8_t)dec_mat[i*k+j];
                l++;
            }
        }

        return 0;
    }

    /*
    //-----------------------------------------------------------------------
    void ECCalc::free_decode_matrix(struct ec_comp *comp)
    {
        free(comp->de_mat);
    }
    */

    //----------------------------------------------------------------------
    int ECCalc::extract_erasures(char *failed_blocks, struct ec_comp *comp){
        char *pt;
        int i = 0, tot = 0;

        pt = strtok (failed_blocks, ",");
        while (pt != NULL) {
            if (i >= k+m) {
                printf("too many data nodes blocks given %d\n", i);
                return -EINVAL;
            }

            if (pt[0] == '1') {
                comp->erasures_arr[i] = 1;
                comp->erasures[i] = 1;
                comp->survived_arr[i] = 0;
                comp->survived[i] = 0;
                if (++tot > m) {
                    printf("too much erasures %d\n", tot);
                    //goto err_survived;
                    return -EINVAL;

                }
            } else {
                comp->erasures_arr[i] = 0;
                comp->erasures[i] = 0;
                comp->survived_arr[i] = 1;
                comp->survived[i] = 1;
            }
            pt = strtok (NULL, ",");
            i++;
        }
        /*
        printf("erasures: ");
        for (i = 0; i < k; i++) {
            printf("[%d]: Jerasure=%d Verbs=%u ", i, comp->erasures_arr[i], comp->erasures[i]);
        }

        printf("\nsurvived: ");
        for (i = 0; i < k + m; i++) {
            printf("[%d]: Jerasure=%d Verbs=%u ", i, comp->survived_arr[i], comp->survived[i]);
        }
        */

        return 0;

    }

}