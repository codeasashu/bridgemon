/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2024, Ashutosh
 *
 * See http://www.asterisk.org for more information about
 * the Asterisk project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

/*! \file
 *
 * \brief BridgeMon() - Monitor bridge events and set BRIDGEPEERID channel variable.
 * \ingroup applications
 *
 * \author Ashutosh
 *
 * \note Based on app_confbridge.c and app_audiofork.c
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#ifndef AST_MODULE
#define AST_MODULE "BridgeMon"
#endif

#include "asterisk.h"

#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/cli.h"
#include "asterisk/app.h"
#include "asterisk/channel.h"
#include "asterisk/bridge.h"
#include "asterisk/bridge_channel.h"
#include "asterisk/bridge_features.h"
#include "asterisk/astobj2.h"
#include "asterisk/manager.h"
#include "asterisk/stringfields.h"

/*** DOCUMENTATION
	<application name="BridgeMon" language="en_US">
		<synopsis>
			Monitors bridge events and sets BRIDGEPEERID channel variable.
		</synopsis>
		<syntax>
			<parameter name="channel_id" required="true">
				<para>The unique ID of the channel to monitor.</para>
			</parameter>
		</syntax>
		<description>
			<para>This application monitors a channel's bridge events and sets the BRIDGEPEERID
			channel variable with the ID of the linked channel when a bridge join event occurs.</para>
			<para>This makes it O(1) to get the linked channel's ID instead of using BRIDGEPEER
			and then querying the channel list API which is O(n).</para>
		</description>
		<see-also>
			<ref type="application">StopBridgeMon</ref>
		</see-also>
	</application>
	<application name="StopBridgeMon" language="en_US">
		<synopsis>
			Stops monitoring bridge events for a channel.
		</synopsis>
		<syntax>
			<parameter name="channel_id" required="false">
				<para>The unique ID of the channel to stop monitoring. If not provided,
				stops monitoring the current channel.</para>
			</parameter>
		</syntax>
		<description>
			<para>Stops monitoring bridge events for the specified channel.</para>
		</description>
		<see-also>
			<ref type="application">BridgeMon</ref>
		</see-also>
	</application>
 ***/

static const char *const app = "BridgeMon";
static const char *const stop_app = "StopBridgeMon";

/* Structure to hold bridge monitoring data */
struct bridgemon_data {
	struct ast_channel *monitored_channel;
	struct ast_bridge_features features;
	char *channel_id;
	ast_mutex_t lock;
	int active;
};

/* Datastore to track bridge monitoring sessions */
struct bridgemon_ds {
	struct bridgemon_data *bridgemon_data;
	ast_mutex_t lock;
	ast_cond_t destruction_condition;
	int destruction_ok;
};

static void bridgemon_ds_destroy(void *data)
{
	struct bridgemon_ds *bridgemon_ds = data;

	ast_mutex_lock(&bridgemon_ds->lock);
	bridgemon_ds->bridgemon_data = NULL;
	bridgemon_ds->destruction_ok = 1;
	ast_cond_signal(&bridgemon_ds->destruction_condition);
	ast_mutex_unlock(&bridgemon_ds->lock);
}

static const struct ast_datastore_info bridgemon_ds_info = {
	.type = "bridgemon",
	.destroy = bridgemon_ds_destroy,
};

/* Bridge hook callback function */
static int bridgemon_hook_callback(struct ast_bridge_channel *bridge_channel, void *data)
{
	struct bridgemon_data *bridgemon_data = data;
	struct ast_channel *monitored_channel;
	struct ast_channel *peer_channel;
	char peer_id[64];

	/* Debug: Hook callback was called */
	ast_verb(2, "BridgeMon: Hook callback called for bridge_channel %p\n", bridge_channel);

	if (!bridgemon_data || !bridgemon_data->active) {
		ast_verb(2, "BridgeMon: Hook callback - bridgemon_data is NULL or not active\n");
		return 0;
	}

	monitored_channel = bridgemon_data->monitored_channel;
	if (!monitored_channel) {
		ast_verb(2, "BridgeMon: Hook callback - monitored_channel is NULL\n");
		return 0;
	}

	/* Debug: Log the channel info */
	ast_verb(2, "BridgeMon: Hook callback - monitored_channel=%s, bridge_channel->chan=%s\n",
		ast_channel_name(monitored_channel), 
		bridge_channel->chan ? ast_channel_name(bridge_channel->chan) : "NULL");
	
	/* Debug: Log the channel IDs for comparison */
	ast_verb(2, "BridgeMon: Hook callback - monitored_channel_id=%s, bridge_channel->chan_id=%s\n",
		ast_channel_uniqueid(monitored_channel), 
		bridge_channel->chan ? ast_channel_uniqueid(bridge_channel->chan) : "NULL");

	/* Check if this is the monitored channel joining */
	if (bridge_channel->chan == monitored_channel) {
		ast_verb(2, "BridgeMon: Hook callback - monitored channel is joining the bridge\n");
		
		/* Debug: Set channel variable to indicate hook was triggered */
		ast_channel_lock(monitored_channel);
		pbx_builtin_setvar_helper(monitored_channel, "BRIDGEMON_HOOK_TRIGGERED", "1");
		ast_channel_unlock(monitored_channel);

		/* Find the peer bridge channel in the bridge */
		struct ast_bridge_channel *peer_bridge_channel = ast_bridge_channel_peer(bridge_channel);
		if (peer_bridge_channel) {
			ast_verb(2, "BridgeMon: Hook callback - found peer bridge channel\n");
			
			/* Get the peer channel from the bridge channel */
			peer_channel = ast_bridge_channel_get_chan(peer_bridge_channel);
			if (peer_channel) {
				ast_verb(2, "BridgeMon: Hook callback - found peer channel: %s\n", ast_channel_name(peer_channel));
				
				/* Get the peer channel's unique ID */
				ast_copy_string(peer_id, ast_channel_uniqueid(peer_channel), sizeof(peer_id));
				
				/* Set the BRIDGEPEERID channel variable on the MONITORED channel (source channel) */
				ast_channel_lock(monitored_channel);
				pbx_builtin_setvar_helper(monitored_channel, "BRIDGEPEERID", peer_id);
				pbx_builtin_setvar_helper(monitored_channel, "BRIDGEMON_PEER_FOUND", "1");
				pbx_builtin_setvar_helper(monitored_channel, "BRIDGEMON_PEER_NAME", ast_channel_name(peer_channel));
				/* Set BRIDGEMON_CHANNEL_ID to the peer channel's ID */
				pbx_builtin_setvar_helper(monitored_channel, "BRIDGEMON_CHANNEL_ID", peer_id);
				ast_channel_unlock(monitored_channel);
				
				ast_verb(2, "BridgeMon: Set BRIDGEPEERID=%s and BRIDGEMON_CHANNEL_ID=%s for monitored channel %s (source channel)\n", 
					peer_id, peer_id, ast_channel_name(monitored_channel));
			} else {
				ast_verb(2, "BridgeMon: Hook callback - failed to get peer channel from bridge channel\n");
			}
		} else {
			ast_verb(2, "BridgeMon: Hook callback - no peer bridge channel found (monitored channel joined first)\n");
		}
	} else {
		/* This is the peer channel joining - check if the monitored channel is already in the bridge */
		ast_verb(2, "BridgeMon: Hook callback - peer channel is joining the bridge\n");
		
		/* Find the monitored channel in the bridge */
		struct ast_bridge_channel *monitored_bridge_channel = ast_bridge_channel_peer(bridge_channel);
		if (monitored_bridge_channel && monitored_bridge_channel->chan == monitored_channel) {
			ast_verb(2, "BridgeMon: Hook callback - found monitored channel in bridge\n");
			
			/* Get the peer channel's unique ID */
			ast_copy_string(peer_id, ast_channel_uniqueid(bridge_channel->chan), sizeof(peer_id));
			
			/* Set the BRIDGEPEERID channel variable on the MONITORED channel (source channel) */
			ast_channel_lock(monitored_channel);
			pbx_builtin_setvar_helper(monitored_channel, "BRIDGEPEERID", peer_id);
			pbx_builtin_setvar_helper(monitored_channel, "BRIDGEMON_PEER_FOUND", "1");
			pbx_builtin_setvar_helper(monitored_channel, "BRIDGEMON_PEER_NAME", ast_channel_name(bridge_channel->chan));
			/* Set BRIDGEMON_CHANNEL_ID to the peer channel's ID */
			pbx_builtin_setvar_helper(monitored_channel, "BRIDGEMON_CHANNEL_ID", peer_id);
			ast_channel_unlock(monitored_channel);
			
			ast_verb(2, "BridgeMon: Set BRIDGEPEERID=%s and BRIDGEMON_CHANNEL_ID=%s for monitored channel %s (source channel) when peer joined\n", 
				peer_id, peer_id, ast_channel_name(monitored_channel));
		} else {
			ast_verb(2, "BridgeMon: Hook callback - monitored channel not found in bridge\n");
		}
	}

	return 0;
}

/* Free bridge monitoring data */
static void bridgemon_data_free(struct bridgemon_data *bridgemon_data)
{
	if (bridgemon_data) {
		ast_mutex_destroy(&bridgemon_data->lock);
		ast_free(bridgemon_data->channel_id);
		ast_bridge_features_cleanup(&bridgemon_data->features);
		ast_free(bridgemon_data);
	}
}

/* Setup datastore for bridge monitoring */
static int setup_bridgemon_ds(struct bridgemon_data *bridgemon_data, struct ast_channel *chan, char **datastore_id)
{
	struct ast_datastore *datastore = NULL;
	struct bridgemon_ds *bridgemon_ds;

	if (!(bridgemon_ds = ast_calloc(1, sizeof(*bridgemon_ds)))) {
		return -1;
	}

	if (ast_asprintf(datastore_id, "%p", bridgemon_ds) == -1) {
		ast_log(LOG_ERROR, "Failed to allocate memory for BridgeMon ID.\n");
		ast_free(bridgemon_ds);
		return -1;
	}

	ast_mutex_init(&bridgemon_ds->lock);
	ast_cond_init(&bridgemon_ds->destruction_condition, NULL);

	if (!(datastore = ast_datastore_alloc(&bridgemon_ds_info, *datastore_id))) {
		ast_mutex_destroy(&bridgemon_ds->lock);
		ast_cond_destroy(&bridgemon_ds->destruction_condition);
		ast_free(bridgemon_ds);
		return -1;
	}

	bridgemon_ds->bridgemon_data = bridgemon_data;
	datastore->data = bridgemon_ds;

	ast_channel_lock(chan);
	ast_channel_datastore_add(chan, datastore);
	ast_channel_unlock(chan);

	return 0;
}

/* Start bridge monitoring for a channel */
static int start_bridgemon(struct ast_channel *chan, const char *channel_id)
{
	struct bridgemon_data *bridgemon_data;
	char *datastore_id = NULL;
	int res = 0;

	/* Debug: Start monitoring function called */
	ast_verb(2, "BridgeMon: start_bridgemon called for channel %s (ID: %s)\n", 
		ast_channel_name(chan), channel_id);

	/* Allocate bridge monitoring data */
	if (!(bridgemon_data = ast_calloc(1, sizeof(*bridgemon_data)))) {
		ast_log(LOG_ERROR, "BridgeMon: Failed to allocate bridgemon_data\n");
		return -1;
	}

	/* Initialize bridge monitoring data */
	ast_mutex_init(&bridgemon_data->lock);
	bridgemon_data->monitored_channel = chan;
	bridgemon_data->channel_id = ast_strdup(channel_id);
	bridgemon_data->active = 1;

	/* Debug: Set channel variable to indicate monitoring started */
	ast_channel_lock(chan);
	pbx_builtin_setvar_helper(chan, "BRIDGEMON_MONITORING_STARTED", "1");
	ast_channel_unlock(chan);

	/* Initialize bridge features */
	if (ast_bridge_features_init(&bridgemon_data->features)) {
		ast_log(LOG_ERROR, "Failed to initialize bridge features for channel %s\n", ast_channel_name(chan));
		bridgemon_data_free(bridgemon_data);
		return -1;
	}

	/* Setup datastore */
	if (setup_bridgemon_ds(bridgemon_data, chan, &datastore_id)) {
		bridgemon_data_free(bridgemon_data);
		return -1;
	}

	/* Add bridge join hook */
	res = ast_bridge_join_hook(&bridgemon_data->features, bridgemon_hook_callback,
		bridgemon_data, NULL, 0);
	if (res) {
		ast_free(datastore_id);
		bridgemon_data_free(bridgemon_data);
		ast_log(LOG_ERROR, "Couldn't add bridge join hook for channel '%s'\n", ast_channel_name(chan));
		return -1;
	}

	/* Debug: Set channel variable to indicate hook was added */
	ast_channel_lock(chan);
	pbx_builtin_setvar_helper(chan, "BRIDGEMON_HOOK_ADDED", "1");
	ast_channel_unlock(chan);

	ast_verb(2, "BridgeMon: Started monitoring bridge events for channel %s (ID: %s)\n", 
		ast_channel_name(chan), channel_id);

	ast_free(datastore_id);
	return 0;
}

/* Stop bridge monitoring for a channel */
static int stop_bridgemon(struct ast_channel *chan, const char *channel_id)
{
	struct ast_datastore *datastore = NULL;
	struct bridgemon_ds *bridgemon_ds;
	struct bridgemon_data *bridgemon_data;

	ast_channel_lock(chan);
	datastore = ast_channel_datastore_find(chan, &bridgemon_ds_info, channel_id);
	ast_channel_unlock(chan);

	if (!datastore) {
		ast_log(LOG_WARNING, "No bridge monitoring found for channel %s\n", ast_channel_name(chan));
		return -1;
	}

	bridgemon_ds = datastore->data;
	ast_mutex_lock(&bridgemon_ds->lock);
	bridgemon_data = bridgemon_ds->bridgemon_data;
	if (bridgemon_data) {
		bridgemon_data->active = 0;
	}
	ast_mutex_unlock(&bridgemon_ds->lock);

	/* Remove the datastore */
	ast_channel_lock(chan);
	if (!ast_channel_datastore_remove(chan, datastore)) {
		ast_datastore_free(datastore);
	}
	ast_channel_unlock(chan);

	ast_verb(2, "BridgeMon: Stopped monitoring bridge events for channel %s\n", ast_channel_name(chan));
	return 0;
}

/* Main BridgeMon application */
static int bridgemon_exec(struct ast_channel *chan, const char *data)
{
	char *parse;
	AST_DECLARE_APP_ARGS(args, AST_APP_ARG(channel_id););

	/* Debug: App is being called */
	ast_verb(2, "BridgeMon: App called for channel %s with data: %s\n", 
		ast_channel_name(chan), S_OR(data, "NULL"));

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "BridgeMon requires a channel ID argument\n");
		return -1;
	}

	parse = ast_strdupa(data);
	AST_STANDARD_APP_ARGS(args, parse);

	if (ast_strlen_zero(args.channel_id)) {
		ast_log(LOG_WARNING, "BridgeMon requires a channel ID argument\n");
		return -1;
	}

	/* Debug: Set a channel variable to indicate BridgeMon was called */
	ast_channel_lock(chan);
	pbx_builtin_setvar_helper(chan, "BRIDGEMON_APP_CALLED", "1");
	pbx_builtin_setvar_helper(chan, "BRIDGEMON_SOURCE_CHANNEL_ID", args.channel_id);
	ast_channel_unlock(chan);

	ast_verb(2, "BridgeMon: Starting monitoring for channel %s (ID: %s)\n", 
		ast_channel_name(chan), args.channel_id);

	/* Start monitoring bridge events for this channel */
	return start_bridgemon(chan, args.channel_id);
}

/* Stop BridgeMon application */
static int stop_bridgemon_exec(struct ast_channel *chan, const char *data)
{
	char *parse;
	AST_DECLARE_APP_ARGS(args, AST_APP_ARG(channel_id););

	parse = ast_strdupa(data);
	AST_STANDARD_APP_ARGS(args, parse);

	/* Stop monitoring bridge events for this channel */
	return stop_bridgemon(chan, S_OR(args.channel_id, ast_channel_uniqueid(chan)));
}

/* CLI command handler */
static char *handle_cli_bridgemon(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ast_channel *chan;

	switch (cmd) {
		case CLI_INIT:
			e->command = "bridgemon {start|stop}";
			e->usage =
				"Usage: bridgemon start <channel_name> [channel_id]\n"
				"         Start monitoring bridge events for a channel.\n"
				"       bridgemon stop <channel_name> [channel_id]\n"
				"         Stop monitoring bridge events for a channel.\n";
			return NULL;
		case CLI_GENERATE:
			return ast_complete_channels(a->line, a->word, a->pos, a->n, 2);
	}

	if (a->argc < 3) {
		return CLI_SHOWUSAGE;
	}

	if (!(chan = ast_channel_get_by_name_prefix(a->argv[2], strlen(a->argv[2])))) {
		ast_cli(a->fd, "No channel matching '%s' found.\n", a->argv[2]);
		return CLI_SUCCESS;
	}

	if (!strcasecmp(a->argv[1], "start")) {
		const char *channel_id = (a->argc >= 4) ? a->argv[3] : ast_channel_uniqueid(chan);
		start_bridgemon(chan, channel_id);
	} else if (!strcasecmp(a->argv[1], "stop")) {
		const char *channel_id = (a->argc >= 4) ? a->argv[3] : ast_channel_uniqueid(chan);
		stop_bridgemon(chan, channel_id);
	} else {
		chan = ast_channel_unref(chan);
		return CLI_SHOWUSAGE;
	}

	chan = ast_channel_unref(chan);
	return CLI_SUCCESS;
}

/* Manager action for BridgeMon */
static int manager_bridgemon(struct mansession *s, const struct message *m)
{
	struct ast_channel *c;
	const char *name = astman_get_header(m, "Channel");
	const char *id = astman_get_header(m, "ActionID");
	const char *channel_id = astman_get_header(m, "ChannelID");
	int res;

	if (ast_strlen_zero(name)) {
		astman_send_error(s, m, "No channel specified");
		return AMI_SUCCESS;
	}

	c = ast_channel_get_by_name(name);
	if (!c) {
		astman_send_error(s, m, "No such channel");
		return AMI_SUCCESS;
	}

	if (ast_strlen_zero(channel_id)) {
		channel_id = ast_channel_uniqueid(c);
	}

	res = start_bridgemon(c, channel_id);
	if (res) {
		ast_channel_unref(c);
		astman_send_error(s, m, "Could not start bridge monitoring");
		return AMI_SUCCESS;
	}

	astman_append(s, "Response: Success\r\n");
	if (!ast_strlen_zero(id)) {
		astman_append(s, "ActionID: %s\r\n", id);
	}
	astman_append(s, "\r\n");

	ast_channel_unref(c);
	return AMI_SUCCESS;
}

/* Manager action for StopBridgeMon */
static int manager_stop_bridgemon(struct mansession *s, const struct message *m)
{
	struct ast_channel *c;
	const char *name = astman_get_header(m, "Channel");
	const char *id = astman_get_header(m, "ActionID");
	const char *channel_id = astman_get_header(m, "ChannelID");
	int res;

	if (ast_strlen_zero(name)) {
		astman_send_error(s, m, "No channel specified");
		return AMI_SUCCESS;
	}

	c = ast_channel_get_by_name(name);
	if (!c) {
		astman_send_error(s, m, "No such channel");
		return AMI_SUCCESS;
	}

	if (ast_strlen_zero(channel_id)) {
		channel_id = ast_channel_uniqueid(c);
	}

	res = stop_bridgemon(c, channel_id);
	if (res) {
		ast_channel_unref(c);
		astman_send_error(s, m, "Could not stop bridge monitoring");
		return AMI_SUCCESS;
	}

	astman_append(s, "Response: Success\r\n");
	if (!ast_strlen_zero(id)) {
		astman_append(s, "ActionID: %s\r\n", id);
	}
	astman_append(s, "\r\n");

	ast_channel_unref(c);
	return AMI_SUCCESS;
}

/* CLI entries */
static struct ast_cli_entry cli_bridgemon[] = {
	AST_CLI_DEFINE(handle_cli_bridgemon, "Execute a BridgeMon command")
};

/* Module load function */
static int load_module(void)
{
	int res;

	ast_verb(2, "BridgeMon: Module loading...\n");

	ast_cli_register_multiple(cli_bridgemon, ARRAY_LEN(cli_bridgemon));
	res = ast_register_application_xml(app, bridgemon_exec);
	res |= ast_register_application_xml(stop_app, stop_bridgemon_exec);
	res |= ast_manager_register_xml("BridgeMon", EVENT_FLAG_SYSTEM, manager_bridgemon);
	res |= ast_manager_register_xml("StopBridgeMon", EVENT_FLAG_SYSTEM, manager_stop_bridgemon);

	if (res == 0) {
		ast_verb(2, "BridgeMon: Module loaded successfully\n");
	} else {
		ast_verb(2, "BridgeMon: Module load failed with error %d\n", res);
	}

	return res;
}

/* Module unload function */
static int unload_module(void)
{
	int res;

	ast_cli_unregister_multiple(cli_bridgemon, ARRAY_LEN(cli_bridgemon));
	res = ast_unregister_application(stop_app);
	res |= ast_unregister_application(app);
	res |= ast_manager_unregister("BridgeMon");
	res |= ast_manager_unregister("StopBridgeMon");

	return res;
}

AST_MODULE_INFO(
	ASTERISK_GPL_KEY, 
	AST_MODFLAG_DEFAULT,
	"Bridge Monitoring application",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
);
