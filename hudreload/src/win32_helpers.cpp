// Windows-only helpers, kept out of schemereload.cpp so <windows.h> never meets the SDK headers.
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef void (*SR_TickFn)();

static SR_TickFn s_tick;
static UINT_PTR s_timer;

static void CALLBACK SR_TimerProc( HWND, UINT, UINT_PTR, DWORD )
{
	if ( s_tick )
		s_tick();
}

// Thread timer on the game's main thread: the engine's message pump dispatches it,
// so the callback runs on the main thread even at the main menu (no map loaded).
extern "C" bool SR_StartTimer( SR_TickFn fn, unsigned ms )
{
	s_tick = fn;
	s_timer = SetTimer( NULL, 0, ms, SR_TimerProc );
	return s_timer != 0;
}

extern "C" void SR_StopTimer()
{
	if ( s_timer )
		KillTimer( NULL, s_timer );
	s_timer = 0;
	s_tick = NULL;
}

// Newest write time of any *.res file in the folder that holds filePath,
// so edits to a #base file next to the scheme also count.
extern "C" unsigned long long SR_NewestResTime( const char *filePath )
{
	char dir[1024];
	strncpy( dir, filePath, sizeof( dir ) - 1 );
	dir[sizeof( dir ) - 1] = 0;
	char *slash = strrchr( dir, '\\' );
	char *fwd = strrchr( dir, '/' );
	if ( fwd > slash )
		slash = fwd;
	if ( !slash )
		return 0;
	slash[1] = 0;

	char pattern[1100];
	_snprintf( pattern, sizeof( pattern ), "%s*.res", dir );
	pattern[sizeof( pattern ) - 1] = 0;

	WIN32_FIND_DATAA fd;
	HANDLE h = FindFirstFileA( pattern, &fd );
	if ( h == INVALID_HANDLE_VALUE )
		return 0;
	unsigned long long newest = 0;
	do
	{
		unsigned long long t = ( (unsigned long long)fd.ftLastWriteTime.dwHighDateTime << 32 ) | fd.ftLastWriteTime.dwLowDateTime;
		if ( t > newest )
			newest = t;
	} while ( FindNextFileA( h, &fd ) );
	FindClose( h );
	return newest;
}

// Last write time of one file (0 if missing).
extern "C" unsigned long long SR_FileTime( const char *path )
{
	WIN32_FILE_ATTRIBUTE_DATA d;
	if ( !GetFileAttributesExA( path, GetFileExInfoStandard, &d ) )
		return 0;
	return ( (unsigned long long)d.ftLastWriteTime.dwHighDateTime << 32 ) | d.ftLastWriteTime.dwLowDateTime;
}

// Writes a temp file and swaps it in, so a reader never sees half a file.
extern "C" bool SR_WriteFileAtomic( const char *path, const char *data, int len )
{
	char tmp[1100];
	_snprintf( tmp, sizeof( tmp ), "%s.tmp", path );
	tmp[sizeof( tmp ) - 1] = 0;
	HANDLE h = CreateFileA( tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL );
	if ( h == INVALID_HANDLE_VALUE )
		return false;
	DWORD n = 0;
	bool ok = WriteFile( h, data, (DWORD)len, &n, NULL ) && n == (DWORD)len;
	CloseHandle( h );
	return ok && MoveFileExA( tmp, path, MOVEFILE_REPLACE_EXISTING );
}

// Runs fn and swallows a crash (access violation etc.) instead of taking the game down.
extern "C" int SR_SafeCall( void ( *fn )() )
{
	__try
	{
		fn();
		return 1;
	}
	__except ( EXCEPTION_EXECUTE_HANDLER )
	{
		return 0;
	}
}
