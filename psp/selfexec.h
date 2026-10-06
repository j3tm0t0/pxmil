#ifndef PXMIL_PSP_SELFEXEC_H
#define PXMIL_PSP_SELFEXEC_H

/*
 * 指定の EBOOT.PBP を起動する (テスト終了時に pspbrew.dev へ戻る用)。
 * WLAN 接続を切ってから CFW の SystemCtrl 経由で LoadExec する
 * (接続したままだと次のインスタンスの IP 取得がタイムアウトする。
 * 素の sceKernelLoadExec は user モード homebrew では拒否される)。
 * 失敗時のみ戻る。
 */
#ifdef __cplusplus
extern "C" {
#endif
void exec_eboot(const char *eboot);
#ifdef __cplusplus
}
#endif

#endif
