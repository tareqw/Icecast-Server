/* Icecast
 *
 * This program is distributed under the GNU General Public License, version 2.
 * A copy of this license is included with this source.
 *
 * Copyright 2000-2004, Jack Moffitt <jack@xiph.org, 
 *                      Michael Smith <msmith@xiph.org>,
 *                      oddsock <oddsock@xiph.org>,
 *                      Karl Heyes <karl@xiph.org>
 *                      and others (see AUTHORS for details).
 */

/* client.c
**
** client interface implementation
**
*/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdlib.h>
#include <string.h>

#include "thread/thread.h"
#include "avl/avl.h"
#include "httpp/httpp.h"

#include "connection.h"
#include "refbuf.h"

#include "client.h"
#include "logging.h"

#undef CATMODULE
#define CATMODULE "client"

client_t *client_create(connection_t *con, http_parser_t *parser)
{
    client_t *client = (client_t *)calloc(1, sizeof(client_t));

    client->con = con;
    client->parser = parser;
    client->queue = NULL;
    client->pos = 0;
    client->burst_sent = 0;

    return client;
}

void client_destroy(client_t *client)
{
    refbuf_t *refbuf;

    if (client == NULL)
        return;
    /* write log entry if ip is set (some things don't set it, like outgoing 
     * slave requests
     */
    if(client->con->ip)
        logging_access(client);
    
    connection_close(client->con);
    httpp_destroy(client->parser);

    while ((refbuf = refbuf_queue_remove(&client->queue)))
        refbuf_release(refbuf);

    /* we need to free client specific format data (if any) */
    if (client->free_client_data)
        client->free_client_data (client);

    free(client->username);

    free(client);
}

void client_send_400(client_t *client, char *message) {
    int bytes;
    bytes = sock_write(client->con->sock, "HTTP/1.0 400 Bad Request\r\n"
            "Content-Type: text/html\r\n\r\n"
            "<b>%s</b>\r\n", message);
    if(bytes > 0) client->con->sent_bytes = bytes;
    client->respcode = 400;
    client_destroy(client);
}

void client_send_404(client_t *client, char *message) {

    int bytes;
    bytes = sock_write(client->con->sock, "HTTP/1.0 404 File Not Found\r\n"
            "Content-Type: text/html\r\n\r\n"
            "<b>%s</b>\r\n", message);
    if(bytes > 0) client->con->sent_bytes = bytes;
    client->respcode = 404;
    client_destroy(client);
}

void client_send_504(client_t *client, char *message) {
    int bytes;
    client->respcode = 504;
    bytes = sock_write(client->con->sock, 
            "HTTP/1.0 504 Server Full\r\n"
            "Content-Type: text/html\r\n\r\n"
            "<b>%s</b>\r\n", message);
       if (bytes > 0) client->con->sent_bytes = bytes;
    client_destroy(client);
}

void client_send_401(client_t *client) {
    int bytes = sock_write(client->con->sock, 
            "HTTP/1.0 401 Authentication Required\r\n"
            "WWW-Authenticate: Basic realm=\"Icecast2 Server\"\r\n"
            "\r\n"
            "You need to authenticate\r\n");
    if(bytes > 0) client->con->sent_bytes = bytes;
    client->respcode = 401;
    client_destroy(client);
}

void client_send_403(client_t *client) {
    int bytes = sock_write(client->con->sock, 
            "HTTP/1.0 403 Forbidden\r\n"
            "\r\n"
            "Access restricted.\r\n");
    if(bytes > 0) client->con->sent_bytes = bytes;
    client->respcode = 403;
    client_destroy(client);
}


/* helper function for sending the data to a client */
int client_send_bytes (client_t *client, const void *buf, unsigned len)
{
    int ret = sock_write_bytes (client->con->sock, buf, len);
    if (ret < 0 && !sock_recoverable (sock_error()))
    {
        DEBUG0 ("Client connection died");
        client->con->error = 1;
    }
    if (ret > 0)
        client->con->sent_bytes += ret;
    return ret;
}

