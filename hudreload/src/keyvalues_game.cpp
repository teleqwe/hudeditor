//-----------------------------------------------------------------------------
// The SDK's KeyValues.cpp, compiled so the KeyValues this plugin makes are laid out the way the game expects.
//
// The game's KeyValues (its tier1, 2024 x64 build) are 0x48 bytes; the SDK's here are 0x40. The game's constructor
// clears a byte at +0x24 and a pointer at +0x40 (a 0x78-byte object it owns); when it deletes a KeyValues it frees
// that object if the byte is set (vgui2.dll's KeyValues teardown). The SDK's constructor never touches either, so a
// KeyValues this plugin made and the game later deleted (the "PressButton" message of schemereload_press, keys a
// reload adds to a scheme the game frees at quit) carried whatever the shared memory pool block held before: the game
// then freed a stale pointer and the heap broke (crash dumps in vgui2.dll and ntdll at quit, and on pressing team
// select's T button). So every KeyValues made here gets the game's full size, all zeroed, before it's built.
//-----------------------------------------------------------------------------
#include <string.h>
#include "vstdlib/IKeyValuesSystem.h"

static const int GAME_KEYVALUES_SIZE = 0x48;

class CGameSizedKeyValuesSystem : public IKeyValuesSystem
{
public:
	void RegisterSizeofKeyValues( int size ) { ::KeyValuesSystem()->RegisterSizeofKeyValues( size ); }
	void *AllocKeyValuesMemory( int size )
	{
		if ( size < GAME_KEYVALUES_SIZE )
			size = GAME_KEYVALUES_SIZE;
		void *p = ::KeyValuesSystem()->AllocKeyValuesMemory( size );
		if ( p )
			memset( p, 0, size );
		return p;
	}
	void FreeKeyValuesMemory( void *pMem ) { ::KeyValuesSystem()->FreeKeyValuesMemory( pMem ); }
	HKeySymbol GetSymbolForString( const char *name, bool bCreate = true ) { return ::KeyValuesSystem()->GetSymbolForString( name, bCreate ); }
	const char *GetStringForSymbol( HKeySymbol symbol ) { return ::KeyValuesSystem()->GetStringForSymbol( symbol ); }
	void AddKeyValuesToMemoryLeakList( void *pMem, HKeySymbol name ) { ::KeyValuesSystem()->AddKeyValuesToMemoryLeakList( pMem, name ); }
	void RemoveKeyValuesFromMemoryLeakList( void *pMem ) { ::KeyValuesSystem()->RemoveKeyValuesFromMemoryLeakList( pMem ); }
	void AddFileKeyValuesToCache( const KeyValues *_kv, const char *resourceName, const char *pathID ) { ::KeyValuesSystem()->AddFileKeyValuesToCache( _kv, resourceName, pathID ); }
	bool LoadFileKeyValuesFromCache( KeyValues *_outKv, const char *resourceName, const char *pathID, IBaseFileSystem *filesystem ) const
	{
		return ::KeyValuesSystem()->LoadFileKeyValuesFromCache( _outKv, resourceName, pathID, filesystem );
	}
	void InvalidateCache() { ::KeyValuesSystem()->InvalidateCache(); }
	void InvalidateCacheForFile( const char *resourceName, const char *pathID ) { ::KeyValuesSystem()->InvalidateCacheForFile( resourceName, pathID ); }
	void SetKeyValuesExpressionSymbol( const char *name, bool bValue ) { ::KeyValuesSystem()->SetKeyValuesExpressionSymbol( name, bValue ); }
	bool GetKeyValuesExpressionSymbol( const char *name ) { return ::KeyValuesSystem()->GetKeyValuesExpressionSymbol( name ); }
	HKeySymbol GetSymbolForStringCaseSensitive( HKeySymbol &hCaseInsensitiveSymbol, const char *name, bool bCreate = true )
	{
		return ::KeyValuesSystem()->GetSymbolForStringCaseSensitive( hCaseInsensitiveSymbol, name, bCreate );
	}
};
static CGameSizedKeyValuesSystem s_GameSizedKeyValuesSystem;

// (IKeyValuesSystem.h is already in, so KeyValues.cpp's own include of it adds nothing: every KeyValuesSystem() in it
// is this one)
#define KeyValuesSystem() ( static_cast< IKeyValuesSystem * >( &s_GameSizedKeyValuesSystem ) )
#include "tier1/KeyValues.cpp" // (the SDK folder is on the include path, see build.bat)
