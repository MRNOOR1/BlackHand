/*
 * rx_queue.h — tiny thread-safe ring buffer of received LoRa messages.
 *
 * The RX poll thread pushes decoded packets here; IPC clients pop them via
 * lora_pop_rx. Fixed capacity, no dynamic allocation. When full, the oldest
 * message is dropped (newest wins) so a slow reader can never block RX.
 */
#ifndef RX_QUEUE_H
#define RX_QUEUE_H

#include <stddef.h>

#define RXQ_CAPACITY   16    /* buffered messages */
#define RXQ_MSG_MAX    250   /* bytes of text per message (matches SX1276) */

/* Store one message (len bytes). Data need not be null-terminated. */
void rxq_push(const unsigned char *data, size_t len);

/*
 * Pop the oldest message into out (null-terminated). Returns the length on
 * success, or -1 when the queue is empty. out must hold RXQ_MSG_MAX+1 bytes.
 */
int rxq_pop(char *out, size_t out_size);

/* Number of messages currently queued. */
int rxq_count(void);

#endif /* RX_QUEUE_H */
