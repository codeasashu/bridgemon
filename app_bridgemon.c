/*! \file
 *
 * \brief FindPeer() - Monitor bridge events and set BRIDGEPEERID channel variable.
 * \ingroup applications
 *
 * \author Ashutosh
 *
 * \note Based on app_confbridge.c
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/


#include "asterisk.h"
#include "asterisk/module.h"
#include "asterisk/channel.h"

/*** DOCUMENTATION
	<application name="FindPeer" language="en_US">
		<synopsis>
			Tags the source channel with peer chanid
		</synopsis>
		<syntax />
		<description>
			<para>This application tags the source chan of the call with peer chan id</para>
		</description>
	</application>
 ***/

static const char app[] = "FindPeer";

static int findpeer_exec(struct ast_channel *chan, const char *data)
{
	struct ast_channel *tchan;
    char buf[64];
    char peer_id[64];

	if (!chan)
		return 0;

    // 		chan = ast_channel_get_by_name(channel_id);
    //
	// parker = ast_channel_get_by_name(data->parker_uuid);
	// if (!parker) {
	// 	return;
	// }
	//

	if (ast_strlen_zero(ast_channel_linkedid(chan))) {
	    ast_verb(2, "FindPeer: [%s] empty linkedid, skipping\n",
		    ast_channel_name(chan));
        return 0;
    }

	ast_verb(2, "FindPeer: handling chan=%s, linkedid=%s\n",
		ast_channel_name(chan), ast_channel_linkedid(chan));

	ast_copy_string(buf, ast_channel_linkedid(chan), sizeof(buf));
	tchan = ast_channel_get_by_name(buf);
	if (!tchan) {
	    ast_verb(2, "FindPeer: [%s] error finding target chan %s\n",
		    ast_channel_name(chan), buf);

		return 0;
	}

	ast_copy_string(peer_id, ast_channel_uniqueid(chan), sizeof(peer_id));
	ast_channel_lock(tchan);
	pbx_builtin_setvar_helper(tchan, "BRIDGEPEERID", peer_id);
	ast_channel_unlock(tchan);
    return 0;
}

static int unload_module(void)
{
	return ast_unregister_application(app);
}

static int load_module(void)
{
	return ast_register_application_xml(app, findpeer_exec);
}

AST_MODULE_INFO(
	ASTERISK_GPL_KEY, 
	AST_MODFLAG_DEFAULT,
	"Find Peer application",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
);
