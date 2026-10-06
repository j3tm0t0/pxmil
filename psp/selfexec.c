/* px68k の psp/selfexec.c から移植 (log/me 依存を除去)。
 * 終了時に pspbrew.dev へ戻るために使う。 */

#include <pspkernel.h>
#include <psploadexec.h>
#include <pspnet_apctl.h>
#include <systemctrl.h>
#include <string.h>

#include "selfexec.h"

void exec_eboot(const char *eboot)
{
	struct SceKernelLoadExecVSHParam vsh;
	struct SceKernelLoadExecParam param;
	int state, i;

	sceNetApctlDisconnect();
	for (i = 0; i < 60; i++) {
		if (sceNetApctlGetState(&state) != 0 || state == PSP_NET_APCTL_STATE_DISCONNECTED)
			break;
		sceKernelDelayThread(50 * 1000);
	}

	/* Ms2 = memory stick homebrew; Ef2 = PSP Go internal storage. */
	memset(&vsh, 0, sizeof(vsh));
	vsh.size = sizeof(vsh);
	vsh.args = strlen(eboot) + 1;
	vsh.argp = (void *)eboot;
	vsh.key = "game";
	if (strncmp(eboot, "ef0:", 4) == 0)
		sctrlKernelLoadExecVSHEf2(eboot, &vsh);
	else
		sctrlKernelLoadExecVSHMs2(eboot, &vsh);

	/* CFW 経由が失敗したときの最後の手段 (user モードでは普通拒否される) */
	memset(&param, 0, sizeof(param));
	param.size = sizeof(param);
	param.args = strlen(eboot) + 1;
	param.argp = (void *)eboot;
	sceKernelLoadExec(eboot, &param);
}
