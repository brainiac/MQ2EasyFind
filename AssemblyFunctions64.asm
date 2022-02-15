
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


; virtual class eqlib::CXRect eqlib::detail::CXWndTrampoline<class eqlib::CFindLocationWnd>::GetMinimizedRect(void)const
make_override 0x58, ?GetMinimizedRect@?$CXWndTrampoline@VCFindLocationWnd@eqlib@@@detail@eqlib@@UEBA?AVCXRect@3@XZ

; virtual class eqlib::CXStr eqlib::detail::CXWndTrampoline<class eqlib::CFindLocationWnd>::GetTooltip(void)const
make_override 0x1D0, ?GetTooltip@?$CXWndTrampoline@VCFindLocationWnd@eqlib@@@detail@eqlib@@UEBA?AVCXStr@3@XZ

; virtual class eqlib::CXRect eqlib::detail::CXWndTrampoline<class eqlib::CFindLocationWnd>::GetHitTestRect(int)const
make_override 0x1E8, ?GetHitTestRect@?$CXWndTrampoline@VCFindLocationWnd@eqlib@@@detail@eqlib@@UEBA?AVCXRect@3@H@Z

; virtual class eqlib::CXRect eqlib::detail::CXWndTrampoline<class eqlib::CFindLocationWnd>::GetInnerRect(void)const
make_override 0x1F0, ?GetInnerRect@?$CXWndTrampoline@VCFindLocationWnd@eqlib@@@detail@eqlib@@UEBA?AVCXRect@3@XZ

; virtual class eqlib::CXRect eqlib::detail::CXWndTrampoline<class eqlib::CFindLocationWnd>::GetClientRect(void)const
make_override 0x1F8, ?GetClientRect@?$CXWndTrampoline@VCFindLocationWnd@eqlib@@@detail@eqlib@@UEBA?AVCXRect@3@XZ

; virtual class eqlib::CXRect eqlib::detail::CXWndTrampoline<class eqlib::CFindLocationWnd>::GetClientClipRect(void)const
make_override 0x200, ?GetClientClipRect@?$CXWndTrampoline@VCFindLocationWnd@eqlib@@@detail@eqlib@@UEBA?AVCXRect@3@XZ

; virtual class eqlib::CXSize eqlib::detail::CXWndTrampoline<class eqlib::CFindLocationWnd>::GetMinSize(bool)const
make_override 0x208, ?GetMinSize@?$CXWndTrampoline@VCFindLocationWnd@eqlib@@@detail@eqlib@@UEBA?AVCXSize@3@_N@Z

; virtual class eqlib::CXSize eqlib::detail::CXWndTrampoline<class eqlib::CFindLocationWnd>::GetMaxSize(bool)const
make_override 0x210, ?GetMaxSize@?$CXWndTrampoline@VCFindLocationWnd@eqlib@@@detail@eqlib@@UEBA?AVCXSize@3@_N@Z

; virtual class eqlib::CXSize eqlib::detail::CXWndTrampoline<class eqlib::CFindLocationWnd>::GetUntileSize(void)const
make_override 0x218, ?GetUntileSize@?$CXWndTrampoline@VCFindLocationWnd@eqlib@@@detail@eqlib@@UEBA?AVCXSize@3@XZ




