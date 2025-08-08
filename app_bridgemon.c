/*! \file
 *
 * \brief Echo application -- play back what you hear to evaluate latency
 *
 * \author Mark Spencer <markster@digium.com>
 *
 * \ingroup applications
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/file.h"
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
	while (ast_waitfor(chan, -1) > -1) {
		struct ast_frame *f = ast_read(chan);
		if (!f) {
			break;
		}
		f->delivery.tv_sec = 0;
		f->delivery.tv_usec = 0;
		if (f->frametype == AST_FRAME_CONTROL
			&& f->subclass.integer == AST_CONTROL_VIDUPDATE
			&& !fir_sent) {
			if (ast_write(chan, f) < 0) {
				ast_frfree(f);
				goto end;
			}
			fir_sent = 1;
		}
		if (!fir_sent && f->frametype == AST_FRAME_VIDEO) {
			struct ast_frame frame = {
				.frametype = AST_FRAME_CONTROL,
				.subclass.integer = AST_CONTROL_VIDUPDATE,
			};
			ast_write(chan, &frame);
			fir_sent = 1;
		}
		if (f->frametype != AST_FRAME_CONTROL
			&& f->frametype != AST_FRAME_MODEM
			&& f->frametype != AST_FRAME_NULL
			&& ast_write(chan, f)) {
			ast_frfree(f);
			goto end;
		}
		if ((f->frametype == AST_FRAME_DTMF) && (f->subclass.integer == '#')) {
			res = 0;
			ast_frfree(f);
			goto end;
		}
		ast_frfree(f);
	}
end:
	return res;
}

static int unload_module(void)
{
	return ast_unregister_application(app);
}

static int load_module(void)
{
	return ast_register_application_xml(app, findpeer_exec);
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Simple Peer Finder");
