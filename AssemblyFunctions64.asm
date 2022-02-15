
section .text

; The original vtable that we are jumping to
; eqlib::CSidlScreenWnd::VirtualFunctionTable * eqlib::detail::CXWndTrampoline<eqlib::CFindLocationWnd>::s_originalVTable
%define VTABLE ?s_originalVTable@?$CXWndTrampoline@VCFindLocationWnd@eqlib@@@detail@eqlib@@2PEAUVirtualFunctionTable@CSidlScreenWnd@3@EA
extern VTABLE

%macro make_override 2
	global %2
	%2:
		mov rax, [rel VTABLE]
		jmp [rax + %1]

%endmacro

make_override 0x0, ?IsValid@?$CXWndTrampoline@VCFindLocationWnd@eqlib@@@detail@eqlib@@UEBA_NXZ

; intentionally skipped because we can't make this one ourselves.
;make_override 0x8, ??_E?$CXWndTrampoline@VCFindLocationWnd@eqlib@@@detail@eqlib@@UEAAPEAXI@Z

make_override 0x10, ?DrawNC@?$CXWndTrampoline@VCFindLocationWnd@eqlib@@@detail@eqlib@@UEBAHXZ
make_override 0x18, ?Draw@?$CXWndTrampoline@VCFindLocationWnd@eqlib@@@detail@eqlib@@UEAAHXZ
make_override 0x20, ?PostDraw@?$CXWndTrampoline@VCFindLocationWnd@eqlib@@@detail@eqlib@@UEAAHXZ
make_override 0x28, ?DrawCursor@?$CXWndTrampoline@VCFindLocationWnd@eqlib@@@detail@eqlib@@UEAAHAEBVCXPoint@3@AEBVCXRect@3@AEA_N@Z
make_override 0x30, ?DrawChildItem@?$CXWndTrampoline@VCFindLocationWnd@eqlib@@@detail@eqlib@@UEBAHPEBVCXWnd@3@PEAX@Z
make_override 0x38, ?DrawCaret@?$CXWndTrampoline@VCFindLocationWnd@eqlib@@@detail@eqlib@@UEBAHXZ
make_override 0x40, ?DrawBackground@?$CXWndTrampoline@VCFindLocationWnd@eqlib@@@detail@eqlib@@UEBAHXZ
make_override 0x48, ?DrawTooltip@?$CXWndTrampoline@VCFindLocationWnd@eqlib@@@detail@eqlib@@UEBAHPEBVCXWnd@3@@Z
make_override 0x50, ?DrawTooltipAtPoint@?$CXWndTrampoline@VCFindLocationWnd@eqlib@@@detail@eqlib@@UEBAHAEBVCXPoint@3@AEBVCXStr@3@@Z

; virtual class eqlib::CXRect eqlib::detail::CXWndTrampoline<class eqlib::CFindLocationWnd>::GetMinimizedRect(void)const
make_override 0x58, ?GetMinimizedRect@?$CXWndTrampoline@VCFindLocationWnd@eqlib@@@detail@eqlib@@UEBA?AVCXRect@3@XZ

make_override 0x60, ?DrawTitleBar@?$CXWndTrampoline@VCFindLocationWnd@eqlib@@@detail@eqlib@@UEBAHAEBVCXRect@3@@Z
make_override 0x68, ?GetCursorToDisplay@?$CXWndTrampoline@VCFindLocationWnd@eqlib@@@detail@eqlib@@UEBAPEAUHICON__@@XZ
make_override 0x70, ?HandleLButtonDown@?$CXWndTrampoline@VCFindLocationWnd@eqlib@@@detail@eqlib@@UEAAHAEBVCXPoint@3@I@Z
make_override 0x78, ?HandleLButtonUp@?$CXWndTrampoline@VCFindLocationWnd@eqlib@@@detail@eqlib@@UEAAHAEBVCXPoint@3@I@Z
make_override 0x80, ?HandleLButtonHeld@?$CXWndTrampoline@VCFindLocationWnd@eqlib@@@detail@eqlib@@UEAAHAEBVCXPoint@3@I@Z
make_override 0x88, ?HandleLButtonUpAfterHeld@?$CXWndTrampoline@VCFindLocationWnd@eqlib@@@detail@eqlib@@UEAAHAEBVCXPoint@3@I@Z
make_override 0x90, ?HandleRButtonDown@?$CXWndTrampoline@VCFindLocationWnd@eqlib@@@detail@eqlib@@UEAAHAEBVCXPoint@3@I@Z
make_override 0x98, ?HandleRButtonUp@?$CXWndTrampoline@VCFindLocationWnd@eqlib@@@detail@eqlib@@UEAAHAEBVCXPoint@3@I@Z
make_override 0xa0, ?HandleRButtonHeld@?$CXWndTrampoline@VCFindLocationWnd@eqlib@@@detail@eqlib@@UEAAHAEBVCXPoint@3@I@Z
make_override 0xa8, ?HandleRButtonUpAfterHeld@?$CXWndTrampoline@VCFindLocationWnd@eqlib@@@detail@eqlib@@UEAAHAEBVCXPoint@3@I@Z
make_override 0xb0, ?HandleWheelButtonDown@?$CXWndTrampoline@VCFindLocationWnd@eqlib@@@detail@eqlib@@UEAAHAEBVCXPoint@3@I@Z
make_override 0xb8, ?HandleWheelButtonUp@?$CXWndTrampoline@VCFindLocationWnd@eqlib@@@detail@eqlib@@UEAAHAEBVCXPoint@3@I@Z
make_override 0xc0, ?HandleMouseMove@?$CXWndTrampoline@VCFindLocationWnd@eqlib@@@detail@eqlib@@UEAAHAEBVCXPoint@3@I@Z
make_override 0xc8, ?HandleWheelMove@?$CXWndTrampoline@VCFindLocationWnd@eqlib@@@detail@eqlib@@UEAAHAEBVCXPoint@3@HI@Z
make_override 0xd0, ?HandleKeyboardMsg@?$CXWndTrampoline@VCFindLocationWnd@eqlib@@@detail@eqlib@@UEAAHII_N@Z
make_override 0xd8, ?HandleMouseLeave@?$CXWndTrampoline@VCFindLocationWnd@eqlib@@@detail@eqlib@@UEAAHXZ
make_override 0xe0, ?OnDragDrop@?$CXWndTrampoline@VCFindLocationWnd@eqlib@@@detail@eqlib@@UEAAHPEAUSDragDropInfo@3@@Z
make_override 0xe8, ?GetDragDropCursor@?$CXWndTrampoline@VCFindLocationWnd@eqlib@@@detail@eqlib@@UEBAPEAUHICON__@@PEAUSDragDropInfo@3@@Z
make_override 0xf0, ?QueryDropOK@?$CXWndTrampoline@VCFindLocationWnd@eqlib@@@detail@eqlib@@UEBA_NPEAUSDragDropInfo@3@@Z
make_override 0xf8, ?OnClickStick@?$CXWndTrampoline@VCFindLocationWnd@eqlib@@@detail@eqlib@@UEAAHPEAVCClickStickInfo@3@I_N@Z
make_override 0x100, ?GetClickStickCursor@?$CXWndTrampoline@VCFindLocationWnd@eqlib@@@detail@eqlib@@UEBAPEAUHICON__@@PEAVCClickStickInfo@3@@Z
make_override 0x108, ?QueryClickStickDropOK@?$CXWndTrampoline@VCFindLocationWnd@eqlib@@@detail@eqlib@@UEBA_NPEAVCClickStickInfo@3@@Z

; virtual int eqlib::detail::CXWndTrampoline<class eqlib::CFindLocationWnd>::WndNotification(class eqlib::CXWnd *, unsigned int, void *)
make_override 0x110, ?WndNotification@?$CXWndTrampoline@VCFindLocationWnd@eqlib@@@detail@eqlib@@UEAAHPEAVCXWnd@3@IPEAX@Z

make_override 0x118, ?OnWndNotification@?$CXWndTrampoline@VCFindLocationWnd@eqlib@@@detail@eqlib@@UEAAXXZ
make_override 0x120, ?Activate@?$CXWndTrampoline@VCFindLocationWnd@eqlib@@@detail@eqlib@@UEAAXXZ
make_override 0x128, ?Deactivate@?$CXWndTrampoline@VCFindLocationWnd@eqlib@@@detail@eqlib@@UEAAXXZ
make_override 0x130, ?OnShow@?$CXWndTrampoline@VCFindLocationWnd@eqlib@@@detail@eqlib@@UEAAHXZ
make_override 0x138, ?OnMove@?$CXWndTrampoline@VCFindLocationWnd@eqlib@@@detail@eqlib@@UEAAHAEBVCXRect@3@@Z
make_override 0x140, ?OnResize@?$CXWndTrampoline@VCFindLocationWnd@eqlib@@@detail@eqlib@@UEAAHHH@Z
make_override 0x148, ?OnBeginMoveOrResize@?$CXWndTrampoline@VCFindLocationWnd@eqlib@@@detail@eqlib@@UEAAHXZ
make_override 0x150, ?OnCompleteMoveOrResize@?$CXWndTrampoline@VCFindLocationWnd@eqlib@@@detail@eqlib@@UEAAHXZ
make_override 0x158, ?OnMinimizeBox@?$CXWndTrampoline@VCFindLocationWnd@eqlib@@@detail@eqlib@@UEAAHXZ
make_override 0x160, ?OnMaximizeBox@?$CXWndTrampoline@VCFindLocationWnd@eqlib@@@detail@eqlib@@UEAAHXZ
make_override 0x168, ?OnTileBox@?$CXWndTrampoline@VCFindLocationWnd@eqlib@@@detail@eqlib@@UEAAHXZ
make_override 0x170, ?OnTile@?$CXWndTrampoline@VCFindLocationWnd@eqlib@@@detail@eqlib@@UEAAHXZ
make_override 0x178, ?OnSetFocus@?$CXWndTrampoline@VCFindLocationWnd@eqlib@@@detail@eqlib@@UEAAHPEAVCXWnd@3@@Z
make_override 0x180, ?OnKillFocus@?$CXWndTrampoline@VCFindLocationWnd@eqlib@@@detail@eqlib@@UEAAHPEAVCXWnd@3@@Z
make_override 0x188, ?OnProcessFrame@?$CXWndTrampoline@VCFindLocationWnd@eqlib@@@detail@eqlib@@UEAAHXZ
make_override 0x190, ?OnVScroll@?$CXWndTrampoline@VCFindLocationWnd@eqlib@@@detail@eqlib@@UEAAHW4EScrollCode@3@H@Z
make_override 0x198, ?OnHScroll@?$CXWndTrampoline@VCFindLocationWnd@eqlib@@@detail@eqlib@@UEAAHW4EScrollCode@3@H@Z
make_override 0x1a0, ?OnBroughtToTop@?$CXWndTrampoline@VCFindLocationWnd@eqlib@@@detail@eqlib@@UEAAHXZ
make_override 0x1a8, ?OnActivate@?$CXWndTrampoline@VCFindLocationWnd@eqlib@@@detail@eqlib@@UEAAHPEAVCXWnd@3@@Z
make_override 0x1b0, ?Show@?$CXWndTrampoline@VCFindLocationWnd@eqlib@@@detail@eqlib@@UEAAH_N00@Z
make_override 0x1b8, ?AboutToShow@?$CXWndTrampoline@VCFindLocationWnd@eqlib@@@detail@eqlib@@UEAA_NXZ
make_override 0x1c0, ?AboutToHide@?$CXWndTrampoline@VCFindLocationWnd@eqlib@@@detail@eqlib@@UEAA_NXZ
make_override 0x1c8, ?RequestDockInfo@?$CXWndTrampoline@VCFindLocationWnd@eqlib@@@detail@eqlib@@UEAAHIPEAVCXWnd@3@PEAVCXRect@3@@Z

; virtual class eqlib::CXStr eqlib::detail::CXWndTrampoline<class eqlib::CFindLocationWnd>::GetTooltip(void)const
make_override 0x1d0, ?GetTooltip@?$CXWndTrampoline@VCFindLocationWnd@eqlib@@@detail@eqlib@@UEBA?AVCXStr@3@XZ

make_override 0x1d8, ?Unknown0x0EC@?$CXWndTrampoline@VCFindLocationWnd@eqlib@@@detail@eqlib@@UEAAXXZ
make_override 0x1e0, ?HitTest@?$CXWndTrampoline@VCFindLocationWnd@eqlib@@@detail@eqlib@@UEBAHAEBVCXPoint@3@PEAH@Z

; virtual class eqlib::CXRect eqlib::detail::CXWndTrampoline<class eqlib::CFindLocationWnd>::GetHitTestRect(int)const
make_override 0x1e8, ?GetHitTestRect@?$CXWndTrampoline@VCFindLocationWnd@eqlib@@@detail@eqlib@@UEBA?AVCXRect@3@H@Z

; virtual class eqlib::CXRect eqlib::detail::CXWndTrampoline<class eqlib::CFindLocationWnd>::GetInnerRect(void)const
make_override 0x1f0, ?GetInnerRect@?$CXWndTrampoline@VCFindLocationWnd@eqlib@@@detail@eqlib@@UEBA?AVCXRect@3@XZ

; virtual class eqlib::CXRect eqlib::detail::CXWndTrampoline<class eqlib::CFindLocationWnd>::GetClientRect(void)const
make_override 0x1f8, ?GetClientRect@?$CXWndTrampoline@VCFindLocationWnd@eqlib@@@detail@eqlib@@UEBA?AVCXRect@3@XZ

; virtual class eqlib::CXRect eqlib::detail::CXWndTrampoline<class eqlib::CFindLocationWnd>::GetClientClipRect(void)const
make_override 0x200, ?GetClientClipRect@?$CXWndTrampoline@VCFindLocationWnd@eqlib@@@detail@eqlib@@UEBA?AVCXRect@3@XZ

; virtual class eqlib::CXSize eqlib::detail::CXWndTrampoline<class eqlib::CFindLocationWnd>::GetMinSize(bool)const
make_override 0x208, ?GetMinSize@?$CXWndTrampoline@VCFindLocationWnd@eqlib@@@detail@eqlib@@UEBA?AVCXSize@3@_N@Z

; virtual class eqlib::CXSize eqlib::detail::CXWndTrampoline<class eqlib::CFindLocationWnd>::GetMaxSize(bool)const
make_override 0x210, ?GetMaxSize@?$CXWndTrampoline@VCFindLocationWnd@eqlib@@@detail@eqlib@@UEBA?AVCXSize@3@_N@Z

; virtual class eqlib::CXSize eqlib::detail::CXWndTrampoline<class eqlib::CFindLocationWnd>::GetUntileSize(void)const
make_override 0x218, ?GetUntileSize@?$CXWndTrampoline@VCFindLocationWnd@eqlib@@@detail@eqlib@@UEBA?AVCXSize@3@XZ

make_override 0x220, ?IsPointTransparent@?$CXWndTrampoline@VCFindLocationWnd@eqlib@@@detail@eqlib@@UEBA_NAEBVCXPoint@3@@Z
make_override 0x228, ?ShouldProcessChildrenFrames@?$CXWndTrampoline@VCFindLocationWnd@eqlib@@@detail@eqlib@@UEBA_NXZ
make_override 0x230, ?ShouldProcessControllerFrame@?$CXWndTrampoline@VCFindLocationWnd@eqlib@@@detail@eqlib@@UEBA_NXZ
make_override 0x238, ?SetDrawTemplate@?$CXWndTrampoline@VCFindLocationWnd@eqlib@@@detail@eqlib@@UEAAXPEAVCXWndDrawTemplate@3@@Z
make_override 0x240, ?UpdateGeometry@?$CXWndTrampoline@VCFindLocationWnd@eqlib@@@detail@eqlib@@UEAAHAEBVCXRect@3@_N111@Z
make_override 0x248, ?Move@?$CXWndTrampoline@VCFindLocationWnd@eqlib@@@detail@eqlib@@UEAAHAEBVCXPoint@3@@Z
make_override 0x250, ?SetWindowText@?$CXWndTrampoline@VCFindLocationWnd@eqlib@@@detail@eqlib@@UEAAXAEBVCXStr@3@@Z
make_override 0x258, ?GetChildWndAt@?$CXWndTrampoline@VCFindLocationWnd@eqlib@@@detail@eqlib@@UEBAPEAVCXWnd@3@AEBVCXPoint@3@_N1@Z
make_override 0x260, ?GetSidlPiece@?$CXWndTrampoline@VCFindLocationWnd@eqlib@@@detail@eqlib@@UEBAPEAVCScreenPieceTemplate@3@AEBVCXStr@3@_N@Z
make_override 0x268, ?GetWindowName@?$CXWndTrampoline@VCFindLocationWnd@eqlib@@@detail@eqlib@@UEBAPEBVCXStr@3@XZ
make_override 0x270, ?SetVScrollPos@?$CXWndTrampoline@VCFindLocationWnd@eqlib@@@detail@eqlib@@UEAAHH@Z
make_override 0x278, ?SetHScrollPos@?$CXWndTrampoline@VCFindLocationWnd@eqlib@@@detail@eqlib@@UEAAHH@Z
make_override 0x280, ?AutoSetVScrollPos@?$CXWndTrampoline@VCFindLocationWnd@eqlib@@@detail@eqlib@@UEAAHVCXRect@3@@Z
make_override 0x288, ?AutoSetHScrollPos@?$CXWndTrampoline@VCFindLocationWnd@eqlib@@@detail@eqlib@@UEAAHVCXRect@3@@Z
make_override 0x290, ?SetAttributesFromSidl@?$CXWndTrampoline@VCFindLocationWnd@eqlib@@@detail@eqlib@@UEAAXPEAVCParamScreenPiece@3@@Z
make_override 0x298, ?OnReloadSidl@?$CXWndTrampoline@VCFindLocationWnd@eqlib@@@detail@eqlib@@UEAAXXZ
make_override 0x2a0, ?HasActivatedFirstTimeAlert@?$CXWndTrampoline@VCFindLocationWnd@eqlib@@@detail@eqlib@@UEBA_NXZ
make_override 0x2a8, ?SetHasActivatedFirstTimeAlert@?$CXWndTrampoline@VCFindLocationWnd@eqlib@@@detail@eqlib@@UEAAX_N@Z
make_override 0x2b0, ?GetMinClientSize@?$CXWndTrampoline@VCFindLocationWnd@eqlib@@@detail@eqlib@@UEBAAEBVCXSize@3@XZ
make_override 0x2b8, ?GetMaxClientSize@?$CXWndTrampoline@VCFindLocationWnd@eqlib@@@detail@eqlib@@UEBAAEBVCXSize@3@XZ
make_override 0x2c0, ?GetActiveEditWnd@?$CXWndTrampoline@VCFindLocationWnd@eqlib@@@detail@eqlib@@UEBAPEAVCEditWnd@3@XZ
make_override 0x2c8, ?UpdateLayout@?$CXWndTrampoline@VCFindLocationWnd@eqlib@@@detail@eqlib@@UEAAX_N@Z



; CSidlScreenWnd virtual functions

make_override 0x2d0, ?OnZone@?$CSidlScreenWndTrampoline@VCFindLocationWnd@eqlib@@@detail@eqlib@@UEAAHXZ
make_override 0x2d8, ?OnPreZone@?$CSidlScreenWndTrampoline@VCFindLocationWnd@eqlib@@@detail@eqlib@@UEAAHXZ
make_override 0x2e0, ?LoadIniInfo@?$CSidlScreenWndTrampoline@VCFindLocationWnd@eqlib@@@detail@eqlib@@UEAAXXZ
make_override 0x2e8, ?StoreIniInfo@?$CSidlScreenWndTrampoline@VCFindLocationWnd@eqlib@@@detail@eqlib@@UEAAXXZ
make_override 0x2f0, ?AsSidlScreenWnd@?$CSidlScreenWndTrampoline@VCFindLocationWnd@eqlib@@@detail@eqlib@@UEAAPEAVCSidlScreenWnd@3@XZ
make_override 0x2f8, ?GetScreenWndType@?$CSidlScreenWndTrampoline@VCFindLocationWnd@eqlib@@@detail@eqlib@@UEAA_NXZ
