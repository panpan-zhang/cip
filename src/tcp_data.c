#include "utils.h"
#include "tcp.h"
#include "list.h"


// Routine for inserting skbs ordered by seq into queue
static void tcp_data_insert_ordered(struct sk_buff_head *queue, struct sk_buff *skb)
{
    struct sk_buff *next;
    struct list_head *item, *tmp;
    list_for_each_safe(item, tmp, &queue->head) {
        next = list_entry(item, struct sk_buff, list);

        if (skb->seq < next->seq) {
            if (skb->end_seq > next->seq) {
                print_err("Could not join skbs\n");
            } else {
                skb->refcnt++;
                skb_queue_add(queue, skb, next);
                return;
            }
        } else if (skb->seq == next->seq) {
            /* We already have this segment! */
            return;
        }
    }

    skb->refcnt++;
    skb_queue_tail(queue, skb);
}

/* Routine for transforming out-of-order segments into order */
static void tcp_consume_ofo_queue(struct tcp_sock *tsk)
{
    struct sock *sk = &tsk->sk;
    struct tcb *tcb = &tsk->tcb;
    struct sk_buff *skb = NULL;

    while ((skb = skb_peek(&tsk->ofo_queue)) != NULL &&
            tcb->rcv_nxt == skb->seq ) {
        tcb->rcv_nxt += skb->dlen;
        skb_dequeue(&tsk->ofo_queue);
        skb_queue_tail(&sk->receive_queue, skb);
    }
}



int tcp_data_dequeue(struct tcp_sock *tsk, void *user_buf, int userlen)
{
    struct sock *sk = &tsk->sk;
    struct tcphdr *th;
    int rlen = 0;

    pthread_mutex_lock(&sk->receive_queue.lock);

    while (!skb_queue_empty(&sk->receive_queue) && rlen < userlen) {
        struct sk_buff *skb = skb_peek(&sk->receive_queue);
        if (skb == NULL) break;

        th = tcp_hdr(skb);
        /* Guard datalen to not overflow userbuf */
        int dlen = (rlen + skb->dlen) > userlen ? (userlen - rlen) : skb->dlen;
        memcpy(user_buf, skb->payload, dlen);

        /* Accommodate next round of data dequeue*/
        skb->dlen -= dlen;
        skb->payload += dlen;
        rlen += dlen;
        user_buf += dlen;

        /* skb if fully eaten, process flags and drop it */
        if (skb->dlen == 0) {
            if (th->psh) tsk->flags |= TCP_PSH; // peer sent all
            skb_dequeue(&sk->receive_queue);
            skb->refcnt--;
            free_skb(skb);
        }
    }

    if (skb_queue_empty(&sk->receive_queue)) {
        sk->poll_event &= ~POLLIN;
    }

    pthread_mutex_unlock(&sk->receive_queue.lock);

    return rlen;
}

int tcp_data_queue(struct tcp_sock *tsk, struct tcphdr *th, struct sk_buff *skb)
{
    struct sock *sk = &tsk->sk;
    struct tcb *tcb = &tsk->tcb;
    int rc = 0;

    if (!tcb->rcv_wnd) {
        free_skb(skb);
        return -1;
    }

    int expected = skb->seq == tcb->rcv_nxt;
    if (expected) {
        tcb->rcv_nxt += skb->dlen;

        skb->refcnt++;
        skb_queue_tail(&sk->receive_queue, skb);
        sk->poll_event |= POLLIN;

        tcp_consume_ofo_queue(tsk);

        tcp_stop_delack_timer(tsk);

        if (th->psh || (skb->dlen == tsk->rmss && ++tsk->delacks > 1)) {
            tsk->delacks = 0;
            tcp_send_ack(sk);
        } else {
            tsk->delack = timer_add(200, &tcp_send_delack, &tsk->sk);
        }
    } else {
        /* Segment passed validation, hence it is in-window
         * but not the left-most sequence. Put into out-of-order queue
         * for later processing */
        tcp_data_insert_ordered(&tsk->ofo_queue, skb);

        /* RFC5581: A TCP receiver SHOULD send an immediate duplicate ACK when a
         * out-of-order segment arrives. The purpose of this ACK is to inform the
         * sender that a segment was received out-of-order and which sequence
         * number is expected. */
        tcp_send_ack(sk);
    }

    return rc;
}
