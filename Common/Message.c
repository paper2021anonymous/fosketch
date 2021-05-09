//
// Created by shangqi on 8/2/20.
//

#include "Message.h"

void pack_message(Message *message, enum message_type type, struct ctx_gcm_s *ctx, uint8_t *payload, int size, int is_enclave) {
    message->header.type = type;
    if(size != 0) {
        message->header.payload_size = GCM_IV_SIZE + size;
    } else {
        message->header.payload_size = 0;
    }


    // has a valid payload
    if(payload != NULL) {
        if(is_enclave) {
            // ocall_alloc_message(message, GCM_IV_SIZE + size);
        } else {
            message->payload = (uint8_t*) malloc(GCM_IV_SIZE + size);
        }

        // attach the IV at the beginning of payload
        memcpy(message->payload, ctx->IV, GCM_IV_SIZE);

        gcm_encrypt(payload, size,
                    ctx->key, ctx->IV,
                    message->payload + GCM_IV_SIZE,
                    message->header.mac);
        incr_ctr(ctx->IV, GCM_IV_SIZE);
    }
}

int unpack_message(Message *message, struct ctx_gcm_s *ctx, uint8_t *res) {
    return gcm_decrypt(message->payload + GCM_IV_SIZE, message->header.payload_size - GCM_IV_SIZE,
                message->header.mac,
                ctx->key, message->payload,
                res);
}

void free_message(Message *message) {
    if(message->payload != NULL) {
        free(message->payload);
    }

    memset(message, 0, sizeof(Message));
}