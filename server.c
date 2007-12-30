/* system includes */
#include <sys/types.h>
#include <netinet/in.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

/* project includes */
#include "db_driver.h"
#include "delegate.h"
#include "log.h"
#include "server.h"

/**
 * Synchronously send a single reply.
 *
 * @param[in] fd connected file descriptor
 * @param[in] p packet to send
 * @param[in] put_packet function to use to send packet
 * @return 0 on success, -1 on failure
 */
static int send_reply(int fd, packet * p, packet_writer put_packet)
{
    int sent = 0;
    packet_status status;

    do {
        status = put_packet(fd, p, &sent);
    } while (status == PACKET_INCOMPLETE);

    if (status == PACKET_ERROR) {
        return -1;
    }

    return 0;
}

/**
 * Synchronously read a single command.
 *
 * @param[in] fd a connected file descriptor
 * @param[in,out] p packet buffer to fill
 * @param[in] get_packet function to use to read packet
 * @return 0 on success, -1 on failure
 */
static int read_command(int fd, packet * p, packet_reader get_packet)
{
    short read_complete = 0;
    p->bytes = 0;

    while (!read_complete) {
        switch (get_packet(fd, p)) {
        case PACKET_EOF:
        case PACKET_ERROR:
            return -1;
        case PACKET_INCOMPLETE:
            read_complete = 0;
            break;
        case PACKET_COMPLETE:
            read_complete = 1;
            break;
        };
    }

    return 0;
}

void server(int fd, struct sockaddr_in *addr)
{
    db_driver_initialize();

    /* establish network-level connections to all delegate databases */
    if (delegate_connect() == -1) {
        lo(LOG_ERROR, "server: error connecting to a delegate: %s",
           strerror(errno));
        return;
    }

    /* loop over conversation between client and delegates */
    while (!db_driver_done()) {
        /* read commands and delegate them */
        while (db_driver_expect_commands()) {
            packet *in_command = packet_new();
            if (!in_command) {
                lo(LOG_ERROR, "server: out of memory!");
                delegate_disconnect();
                return;
            }

            lo(LOG_DEBUG, "server: waiting for next command...");
            if (read_command(fd, in_command, db_driver_get_packet) == -1) {
                if ((errno != ECONNRESET) && (errno != EINPROGRESS)) {
                    lo(LOG_ERROR, "server: error reading command: %s",
                       strerror(errno));
                } else {
                    lo(LOG_DEBUG, "server: client went away");
                }
                packet_delete(in_command);
                delegate_disconnect();
                return;
            }
            db_driver_got_command(in_command);

            lo(LOG_DEBUG, "server: delegating command...");
            if (!delegate_put(db_driver_put_packet, db_driver_rewrite_command,
                              in_command)) {
                lo(LOG_ERROR, "server: error delegating command");
                packet_delete(in_command);
                delegate_disconnect();
                return;
            }

            packet_delete(in_command);
        }

        /* read replies from delegates, reduce and return them */
        while (db_driver_expect_replies()) {
            lo(LOG_DEBUG, "server: waiting for reply...");

            packet_set *replies = delegate_get(db_driver_get_packet);
            if (!replies) {
                lo(LOG_ERROR, "server: error getting delegate replies");
                delegate_disconnect();
                return;
            }
            packet *final_reply = db_driver_reduce_replies(replies);
            packet_set_delete(replies);

            lo(LOG_DEBUG, "server: returning reply...");

            if (send_reply(fd, final_reply, db_driver_put_packet) == -1) {
                lo(LOG_ERROR, "server: error sending reply: %s",
                   strerror(errno));
                packet_delete(final_reply);
                delegate_disconnect();
                return;
            }

            packet_delete(final_reply);
        }

        lo(LOG_DEBUG, "server: done with this conversation.");
    }

    /* teardown all the delegate connections */
    delegate_disconnect();

    lo(LOG_DEBUG, "server: finished work on fd %d", fd);
    return;
}

static component *server_subcomponents[] = {
    SUBCOMPONENT(db_driver),
    SUBCOMPONENT(delegate),
    SUBCOMPONENT_END()
};

component server_component = {
    INITIALIZE_NONE,
    SHUTDOWN_NONE,
    OPTIONS_NONE,
    server_subcomponents
};
