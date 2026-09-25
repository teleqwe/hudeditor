// Vtable slots of a few vgui_controls methods, read from the compiler's own "call virtual N" thunks, so the plugin can
// call them on the game's panels without guessing offsets (slot = byte offset / 8). -1 if the thunk isn't the expected shape.
#include "vgui_controls/Label.h"
#include "vgui_controls/Menu.h"

static int SlotOf( const void *thunk )
{
	const unsigned char *p = (const unsigned char *)thunk;
	if ( p[0] == 0xE9 ) // incremental-link jump to the real thunk
		p += 5 + *(const int *)( p + 1 );
	if ( p[0] != 0x48 || p[1] != 0x8B || p[2] != 0x01 || p[3] != 0xFF ) // mov rax,[rcx]; jmp [rax+...]
		return -1;
	if ( p[4] == 0x20 )
		return 0;
	if ( p[4] == 0x60 )
		return p[5] / 8;
	if ( p[4] == 0xA0 )
		return *(const int *)( p + 5 ) / 8;
	return -1;
}

template < class T > static const void *Thunk( T pmf )
{
	return *(const void **)&pmf;
}

int SlotLabelSetFont() { return SlotOf( Thunk( &vgui::Label::SetFont ) ); }
int SlotLabelGetFont() { return SlotOf( Thunk( &vgui::Label::GetFont ) ); }
int SlotPanelSetInfo() { return SlotOf( Thunk( &vgui::Panel::SetInfo ) ); }
int SlotMenuSetItemHeight() { return SlotOf( Thunk( &vgui::Menu::SetMenuItemHeight ) ); }
int SlotMenuGetItemHeight() { return SlotOf( Thunk( &vgui::Menu::GetMenuItemHeight ) ); }
