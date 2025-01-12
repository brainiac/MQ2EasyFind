%include "eqlib/AssemblyMacros.asm"

section .text

%ifdef TEST

create_window_override_funcs CFindLocationWndOverride, CFindLocationWnd, CGFScreenWnd

%else

create_window_override_funcs CFindLocationWndOverride, CFindLocationWnd, CSidlScreenWnd

%endif
