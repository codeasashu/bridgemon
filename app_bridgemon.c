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


#ifndef AST_MODULE
#define AST_MODULE "FindPeer"
#endif

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
	if (!chan)
		return 0;

	if (ast_strlen_zero(ast_channel_linkedid(chan))) {
	    ast_verb(2, "FindPeer: [%s] empty linkedid, skipping\n",
		    ast_channel_name(chan));
        	return 0;
    	}

	// const char *linkedid = S_OR(ast_channel_linkedid(chan), "");
	const char *linkedid = ast_channel_linkedid(chan);
	if (linkedid) {
		RAII_VAR(struct ast_channel *, bridge, ast_channel_get_by_name(linkedid), ast_channel_cleanup);
		if (!bridge) {
	    	    ast_verb(2, "FindPeer: [%s] no peer found. skipping\n",
		    	ast_channel_name(chan));
        	    return 0;
		}
		ast_verb(2, "FindPeer4: bridge found peer=%s, bridgepeerid=%s\n",
			linkedid, ast_channel_uniqueid(chan));
		ast_channel_lock(bridge);
		pbx_builtin_setvar_helper(bridge, "BRIDGEPEERID", ast_channel_uniqueid(chan));
		ast_channel_unlock(bridge);
	}
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
