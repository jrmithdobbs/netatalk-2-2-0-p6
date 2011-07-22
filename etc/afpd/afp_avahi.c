/*
 * Author:  Daniel S. Haischt <me@daniel.stefan.haischt.name>
 * Purpose: Avahi based Zeroconf support
 * Docs:    http://avahi.org/download/doxygen/
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef HAVE_AVAHI

#include <unistd.h>

#include <avahi-common/strlst.h>

#include <atalk/logger.h>
#include <atalk/util.h>
#include <atalk/dsi.h>
#include <atalk/unicode.h>

#include "afp_avahi.h"
#include "afp_config.h"
#include "volume.h"

/*****************************************************************
 * Global variables
 *****************************************************************/
struct context *ctx = NULL;

/*****************************************************************
 * Private functions
 *****************************************************************/

static void publish_reply(AvahiEntryGroup *g,
                          AvahiEntryGroupState state,
                          void *userdata);

/*
 * This function tries to register the AFP DNS
 * SRV service type.
 */
static void register_stuff(void) {
    uint port;
    const AFPConfig *config;
    const struct vol *volume;
    DSI *dsi;
    char name[MAXINSTANCENAMELEN+1];
    AvahiStringList *strlist = NULL;
    AvahiStringList *strlist2 = NULL;
    char tmpname[256];

    assert(ctx->client);

    if (!ctx->group) {
        if (!(ctx->group = avahi_entry_group_new(ctx->client, publish_reply, ctx))) {
            LOG(log_error, logtype_afpd, "Failed to create entry group: %s",
                avahi_strerror(avahi_client_errno(ctx->client)));
            goto fail;
        }
    }

    if (avahi_entry_group_is_empty(ctx->group)) {
        /* Register our service */

        /* Build AFP volumes list */
        int i = 0;
        strlist = avahi_string_list_add_printf(strlist, "sys=waMa=0,adVF=0x100");
		
        for (volume = getvolumes(); volume; volume = volume->v_next) {

            if (convert_string(CH_UCS2, CH_UTF8_MAC, volume->v_name, -1, tmpname, 255) <= 0) {
                LOG ( log_error, logtype_afpd, "Could not set Zeroconf volume name for TimeMachine");
                goto fail;
            }

            if (volume->v_flags & AFPVOL_TM) {
                if (volume->v_uuid) {
                    LOG(log_info, logtype_afpd, "Registering volume '%s' with UUID: '%s' for TimeMachine",
                        volume->v_localname, volume->v_uuid);
                    strlist = avahi_string_list_add_printf(strlist, "dk%u=adVN=%s,adVF=0xa1,adVU=%s",
                                                           i++, tmpname, volume->v_uuid);
                } else {
                    LOG(log_warning, logtype_afpd, "Registering volume '%s' for TimeMachine. But UUID is invalid.",
                        volume->v_localname);
                    strlist = avahi_string_list_add_printf(strlist, "dk%u=adVN=%s,adVF=0xa1",
                                                           i++, tmpname);
                }	
            }
        }

        /* AFP server */
        for (config = ctx->configs; config; config = config->next) {

            dsi = (DSI *)config->obj.handle;
            port = getip_port((struct sockaddr *)&dsi->server);

            if (convert_string(config->obj.options.unixcharset,
                               CH_UTF8,
                               config->obj.options.server ?
                               config->obj.options.server :
                               config->obj.options.hostname,
                               -1,
                               name,
                               MAXINSTANCENAMELEN) <= 0) {
                LOG(log_error, logtype_afpd, "Could not set Zeroconf instance name");
                goto fail;
            }
            if ((dsi->bonjourname = strdup(name)) == NULL) {
                LOG(log_error, logtype_afpd, "Could not set Zeroconf instance name");
                goto fail;

            }
            LOG(log_info, logtype_afpd, "Registering server '%s' with with Bonjour",
                dsi->bonjourname);

            if (avahi_entry_group_add_service(ctx->group,
                                              AVAHI_IF_UNSPEC,
                                              AVAHI_PROTO_UNSPEC,
                                              0,
                                              dsi->bonjourname,
                                              AFP_DNS_SERVICE_TYPE,
                                              NULL,
                                              NULL,
                                              port,
                                              NULL) < 0) {
                LOG(log_error, logtype_afpd, "Failed to add service: %s",
                    avahi_strerror(avahi_client_errno(ctx->client)));
                goto fail;
            }

            if (i && avahi_entry_group_add_service_strlst(ctx->group,
                                                          AVAHI_IF_UNSPEC,
                                                          AVAHI_PROTO_UNSPEC,
                                                          0,
                                                          dsi->bonjourname,
                                                          ADISK_SERVICE_TYPE,
                                                          NULL,
                                                          NULL,
                                                          9, /* discard */
                                                          strlist) < 0) {
                LOG(log_error, logtype_afpd, "Failed to add service: %s",
                    avahi_strerror(avahi_client_errno(ctx->client)));
                goto fail;
            }	/* if */

            if (config->obj.options.mimicmodel) {
                strlist2 = avahi_string_list_add_printf(strlist2, "model=%s", config->obj.options.mimicmodel);
                if (avahi_entry_group_add_service_strlst(ctx->group,
                                                         AVAHI_IF_UNSPEC,
                                                         AVAHI_PROTO_UNSPEC,
                                                         0,
                                                         dsi->bonjourname,
                                                         DEV_INFO_SERVICE_TYPE,
                                                         NULL,
                                                         NULL,
                                                         0,
                                                         strlist2) < 0) {
                    LOG(log_error, logtype_afpd, "Failed to add service: %s",
                        avahi_strerror(avahi_client_errno(ctx->client)));
                    goto fail;
                }
            } /* if (config->obj.options.mimicmodel) */

        }	/* for config*/

        if (avahi_entry_group_commit(ctx->group) < 0) {
            LOG(log_error, logtype_afpd, "Failed to commit entry group: %s",
                avahi_strerror(avahi_client_errno(ctx->client)));
            goto fail;
        }

    }	/* if avahi_entry_group_is_empty*/

    return;

fail:
    avahi_client_free (ctx->client);
    avahi_threaded_poll_quit(ctx->threaded_poll);
}

/* Called when publishing of service data completes */
static void publish_reply(AvahiEntryGroup *g,
                          AvahiEntryGroupState state,
                          AVAHI_GCC_UNUSED void *userdata)
{
    assert(ctx->group == NULL || g == ctx->group);

    switch (state) {

    case AVAHI_ENTRY_GROUP_ESTABLISHED :
        /* The entry group has been established successfully */
        LOG(log_debug, logtype_afpd, "publish_reply: AVAHI_ENTRY_GROUP_ESTABLISHED");
        break;

    case AVAHI_ENTRY_GROUP_COLLISION:
        /* With multiple names there's no way to know which one collided */
        LOG(log_error, logtype_afpd, "publish_reply: AVAHI_ENTRY_GROUP_COLLISION",
            avahi_strerror(avahi_client_errno(ctx->client)));
        avahi_client_free(avahi_entry_group_get_client(g));
        avahi_threaded_poll_quit(ctx->threaded_poll);
        break;
		
    case AVAHI_ENTRY_GROUP_FAILURE:
        LOG(log_error, logtype_afpd, "Failed to register service: %s",
            avahi_strerror(avahi_client_errno(ctx->client)));
        avahi_client_free(avahi_entry_group_get_client(g));
        avahi_threaded_poll_quit(ctx->threaded_poll);
        break;

    case AVAHI_ENTRY_GROUP_UNCOMMITED:
        break;
    case AVAHI_ENTRY_GROUP_REGISTERING:
        break;
    }
}

static void client_callback(AvahiClient *client,
                            AvahiClientState state,
                            void *userdata)
{
    ctx->client = client;

    switch (state) {
    case AVAHI_CLIENT_S_RUNNING:
        /* The server has startup successfully and registered its host
         * name on the network, so it's time to create our services */
        if (!ctx->group)
            register_stuff();
        break;

    case AVAHI_CLIENT_S_COLLISION:
        if (ctx->group)
            avahi_entry_group_reset(ctx->group);
        break;

    case AVAHI_CLIENT_FAILURE: {
        if (avahi_client_errno(client) == AVAHI_ERR_DISCONNECTED) {
            int error;

            avahi_client_free(ctx->client);
            ctx->client = NULL;
            ctx->group = NULL;

            /* Reconnect to the server */
            if (!(ctx->client = avahi_client_new(avahi_threaded_poll_get(ctx->threaded_poll),
                                                 AVAHI_CLIENT_NO_FAIL,
                                                 client_callback,
                                                 ctx,
                                                 &error))) {

                LOG(log_error, logtype_afpd, "Failed to contact server: %s",
                    avahi_strerror(error));

                avahi_client_free (ctx->client);
                avahi_threaded_poll_quit(ctx->threaded_poll);
            }

        } else {
            LOG(log_error, logtype_afpd, "Client failure: %s",
                avahi_strerror(avahi_client_errno(client)));
            avahi_client_free (ctx->client);
            avahi_threaded_poll_quit(ctx->threaded_poll);
        }
        break;
    }

    case AVAHI_CLIENT_S_REGISTERING:
        break;
    case AVAHI_CLIENT_CONNECTING:
        break;
    }
}

/************************************************************************
 * Public funcions
 ************************************************************************/

/*
 * Tries to setup the Zeroconf thread and any
 * neccessary config setting.
 */
void av_zeroconf_setup(const AFPConfig *configs) {
    int error;

    /* initialize the struct that holds our config settings. */
    if (ctx) {
        LOG(log_debug, logtype_afpd, "Resetting zeroconf records");
        avahi_entry_group_reset(ctx->group);
    } else {
        ctx = calloc(1, sizeof(struct context));
        ctx->configs = configs;
        assert(ctx);
    }

/* first of all we need to initialize our threading env */
    if (!(ctx->threaded_poll = avahi_threaded_poll_new())) {
        goto fail;
    }

/* now we need to acquire a client */
    if (!(ctx->client = avahi_client_new(avahi_threaded_poll_get(ctx->threaded_poll),
                                         AVAHI_CLIENT_NO_FAIL,
                                         client_callback,
                                         NULL,
                                         &error))) {
        LOG(log_error, logtype_afpd, "Failed to create client object: %s",
            avahi_strerror(avahi_client_errno(ctx->client)));
        goto fail;
    }

    return;

fail:
    if (ctx)
        av_zeroconf_unregister();

    return;
}

/*
 * This function finally runs the loop impl.
 */
int av_zeroconf_run(void) {
    /* Finally, start the event loop thread */
    if (avahi_threaded_poll_start(ctx->threaded_poll) < 0) {
        LOG(log_error, logtype_afpd, "Failed to create thread: %s",
            avahi_strerror(avahi_client_errno(ctx->client)));
        goto fail;
    } else {
        LOG(log_info, logtype_afpd, "Successfully started avahi loop.");
    }

    ctx->thread_running = 1;
    return 0;

fail:
    if (ctx)
        av_zeroconf_unregister();

    return -1;
}

/*
 * Tries to shutdown this loop impl.
 * Call this function from outside this thread.
 */
void av_zeroconf_shutdown() {
    /* Call this when the app shuts down */
    avahi_threaded_poll_stop(ctx->threaded_poll);
    avahi_client_free(ctx->client);
    avahi_threaded_poll_free(ctx->threaded_poll);
    free(ctx);
    ctx = NULL;
}

/*
 * Tries to shutdown this loop impl.
 * Call this function from inside this thread.
 */
int av_zeroconf_unregister() {
    if (ctx->thread_running) {
        /* First, block the event loop */
        avahi_threaded_poll_lock(ctx->threaded_poll);

        /* Than, do your stuff */
        avahi_threaded_poll_quit(ctx->threaded_poll);

        /* Finally, unblock the event loop */
        avahi_threaded_poll_unlock(ctx->threaded_poll);
        ctx->thread_running = 0;
    }

    if (ctx->client)
        avahi_client_free(ctx->client);

    if (ctx->threaded_poll)
        avahi_threaded_poll_free(ctx->threaded_poll);

    free(ctx);
    ctx = NULL;

    return 0;
}

#endif /* USE_AVAHI */

