/****
 * Copyright (c) 2016, NVIDIA Corporation.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *    * Redistributions of source code must retain the above copyright notice,
 *      this list of conditions and the following disclaimer.
 *    * Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
 *    * Neither the name of the NVIDIA Corporation nor the names of its
 *      contributors may be used to endorse or promote products derived from
 *      this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 ****/

#pragma once

#ifndef __GDSYNC_H__
#error "don't include directly this header, use gdsync.h always"
#endif

#define GDS_API_MAJOR_VERSION    2U
#define GDS_API_MINOR_VERSION    1U
#define GDS_API_VERSION          ((GDS_API_MAJOR_VERSION << 16) | GDS_API_MINOR_VERSION)
#define GDS_API_VERSION_COMPATIBLE(v) \
    ( ((((v) & 0xffff0000U) >> 16) == GDS_API_MAJOR_VERSION) &&   \
      ((((v) & 0x0000ffffU) >> 0 ) >= GDS_API_MINOR_VERSION) )

typedef enum gds_param {
    GDS_PARAM_VERSION,
    GDS_NUM_PARAMS
} gds_param_t;

int gds_query_param(gds_param_t param, int *value);

enum gds_create_qp_flags {
    GDS_CREATE_QP_DEFAULT      = 0,
    GDS_CREATE_QP_WQ_ON_GPU    = 1<<0,
    GDS_CREATE_QP_TX_CQ_ON_GPU = 1<<1,
    GDS_CREATE_QP_RX_CQ_ON_GPU = 1<<2,
    GDS_CREATE_QP_GPU_INVALIDATE_TX_CQ = 1<<3,
    GDS_CREATE_QP_GPU_INVALIDATE_RX_CQ = 1<<4,
    GDS_CREATE_QP_WQ_DBREC_ON_GPU = 1<<5,
};

typedef struct ibv_qp_init_attr_ex gds_qp_init_attr_t;
typedef struct ibv_exp_send_wr gds_send_wr;

struct gds_cq {
        struct ibv_cq *cq;
        uint32_t curr_offset;
};

struct gds_qp {
        struct ibv_qp *qp;
        struct gds_cq send_cq;
        struct gds_cq recv_cq;
};

// consider enabling GDS_CREATE_QP_GPU_INVALIDATE_T/RX_CQ when
// using GDS_WAIT_CQ_CONSUME_CQE below
struct gds_qp *gds_create_qp(struct ibv_pd *pd, struct ibv_context *context,
                             gds_qp_init_attr_t *qp_init_attr,
                             int gpu_id, int flags);
int gds_destroy_qp(struct gds_qp *qp);

//struct ibv_exp_peer_direct_attr;
//int gds_register_peer(struct ibv_context *context, unsigned gpu_id, struct ibv_exp_peer_direct_attr **p_attr);
int gds_register_peer(struct ibv_context *context, unsigned gpu_id);

/* \brief: CPU-synchronous post send for peer QPs
 *
 * Notes:
 * - this API might have higher overhead than ibv_post_send. 
 * - It is provided for convenience only.
 */
int gds_post_send(struct gds_qp *qp, gds_send_wr *wr, gds_send_wr **bad_wr);

/* \brief: CPU-synchronous post recv for peer QPs
 *
 * Notes:
 * - there is no GPU-synchronous version of this because there is not a use case for it.
 */
int gds_post_recv(struct gds_qp *qp, struct ibv_recv_wr *wr, struct ibv_recv_wr **bad_wr);

// forward decls
enum gds_wait_cq_flags {
    GDS_WAIT_CQ_CONSUME_CQE = 1 // GPU will invalidate CQE after polling on that completes.
                                // In this case, CPU must avoid calling ibv_poll_cq() on that CQ so
                                // to avoid races
};

int gds_stream_wait_cq(CUstream stream, struct gds_cq *cq, int flags);
// same as above, plus writing a trailing flag word efficiently
int gds_stream_wait_cq_ex(CUstream stream, struct gds_cq *cq, int flags, uint32_t *dw, uint32_t val);

/* \brief: GPU stream-synchronous send for peer QPs
 *
 * Notes:
 * - execution of the send operation happens in CUDA stream order
 */
int gds_stream_queue_send(CUstream stream, struct gds_qp *qp, gds_send_wr *p_ewr, gds_send_wr **bad_ewr);
// same as above, plus writing a trailing flag word efficiently
int gds_stream_queue_send_ex(CUstream stream, struct gds_qp *qp, gds_send_wr *p_ewr, gds_send_wr **bad_ewr, uint32_t *dw, uint32_t val);

/* \brief GPU stream-synchronous post recv
 * Notes:
 * - this is not implemented and returns an error
 *   see notes for gds_post_recv
 */
int gds_stream_queue_recv(CUstream stream, struct gds_qp *qp, struct ibv_recv_wr *p_ewr, struct ibv_recv_wr **bad_ewr);


// batched submission APIs

enum {
        GDS_SEND_INFO_MAX_OPS = 32,
        GDS_WAIT_INFO_MAX_OPS = 32
};

typedef struct gds_send_request {
        struct ibv_exp_peer_commit commit;
        struct peer_op_wr wr[GDS_SEND_INFO_MAX_OPS];
} gds_send_request_t;

typedef struct gds_wait_request {
        struct ibv_exp_peer_peek peek;
        struct peer_op_wr wr[GDS_WAIT_INFO_MAX_OPS];
} gds_wait_request_t;

typedef struct gds_value32_descriptor { 
        uint32_t  *ptr;
        uint32_t   value;
        int        cond_flags; // don't care for GDS_TAG_WRITE_VALUE32
        int        flags;
} gds_value32_descriptor_t;

typedef enum gds_tag { GDS_TAG_SEND, GDS_TAG_WAIT, GDS_TAG_WAIT_VALUE32, GDS_TAG_WRITE_VALUE32 } gds_tag_t;

typedef struct gds_descriptor {
        gds_tag_t tag;
        union {
                gds_send_request_t      *send;
                gds_wait_request_t      *wait;
                gds_value32_descriptor_t value32;
        };
} gds_descriptor_t;

int gds_prepare_wait_value32(uint32_t *ptr, uint32_t value, int cond_flags, int flags, gds_value32_descriptor_t *desc);
int gds_stream_post_descriptors(CUstream stream, size_t n_descs, gds_descriptor_t *descs);

int gds_prepare_send(struct gds_qp *qp, gds_send_wr *p_ewr, gds_send_wr **bad_ewr, gds_send_request_t *request);
int gds_stream_post_send(CUstream stream, gds_send_request_t *request);
int gds_stream_post_send_ex(CUstream stream, gds_send_request_t *request, uint32_t *dw, uint32_t val);
int gds_stream_post_send_all(CUstream stream, int count, gds_send_request_t *request);
//int gds_stream_post_send_all_ex(CUstream stream, int count, gds_send_request_t request, uint32_t *dw, uint32_t val);

int gds_prepare_wait_cq(struct gds_cq *cq, gds_wait_request_t *request, int flags);
int gds_stream_post_wait_cq(CUstream stream, gds_wait_request_t *request);
int gds_stream_post_wait_cq_ex(CUstream stream, gds_wait_request_t *request, uint32_t *dw, uint32_t val);
int gds_stream_post_wait_cq_all(CUstream stream, int count, gds_wait_request_t *request);
//int gds_stream_post_wait_cq_all_ex(CUstream stream, int count, gds_wait_request_t request, uint32_t *dw, uint32_t val);
int gds_append_wait_cq(gds_wait_request_t *request, uint32_t *dw, uint32_t val);


/* \brief CPU-synchronously enable polling on request
 *
 * Unblock calls to ibv_poll_cq. CPU will do what is necessary to make the corresponding
 * CQE poll-able.
 *
 */
int gds_post_wait_cq(struct gds_cq *cq, gds_wait_request_t *request, int flags);

/*
 * Local variables:
 *  c-indent-level: 8
 *  c-basic-offset: 8
 *  tab-width: 8
 *  indent-tabs-mode: nil
 * End:
 */
