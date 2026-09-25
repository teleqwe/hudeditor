// CS:S HUD Editor: shows editor/index.html in a WebView2 window, installs + launches the schemereload plugin packed
// inside the exe, and edits a HUD in place (normally a folder in cstrike/custom). The plugin puts that folder first in
// the game's search paths, so edits show up live. Until they are saved, the files' saved versions wait in g_unsaved and
// go back if the changes are discarded.
#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shlwapi.h>
#include <tlhelp32.h>
#include <wrl.h>
#include <string>
#include <vector>
#include <algorithm>
#include <filesystem>
#include <mutex>
#include <d3d11.h>
#include <dxgi.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include "WebView2.h"

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;
using std::string;
using std::wstring;

static HWND g_hwnd;
static ComPtr<ICoreWebView2Environment> g_env;
static ComPtr<ICoreWebView2Controller> g_ctl;
static ComPtr<ICoreWebView2> g_web;
static wstring g_game;        // ...\Counter-Strike Source\cstrike
static wstring g_custom;      // g_game\custom, or the --selftest folder
static wstring g_hudPath;     // the HUD being edited
static wstring g_unsaved;     // saved versions of the files changed since the last save (see KeepSaved)
static wstring g_settings;    // recent HUD folders, most recent first
static wstring g_steamExe;
static wstring g_selftestOut; // set in --selftest mode
static int g_exitCode;
static const wchar_t *GAME_TITLE = L"Counter-Strike Source";

// ---------- small helpers ----------
static string Res( const wchar_t *name )
{
	HRSRC r = FindResourceW( NULL, name, (LPCWSTR)RT_RCDATA );
	if ( !r )
		return string();
	return string( (const char *)LockResource( LoadResource( NULL, r ) ), SizeofResource( NULL, r ) );
}

static wstring Widen( const string &s, UINT cp = CP_UTF8, DWORD flags = 0 )
{
	int n = MultiByteToWideChar( cp, flags, s.data(), (int)s.size(), NULL, 0 );
	wstring w( n, 0 );
	MultiByteToWideChar( cp, flags, s.data(), (int)s.size(), &w[0], n );
	return w;
}

static string Utf8( const wstring &w )
{
	int n = WideCharToMultiByte( CP_UTF8, 0, w.data(), (int)w.size(), NULL, 0, NULL, NULL );
	string s( n, 0 );
	WideCharToMultiByte( CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, NULL, NULL );
	return s;
}

static bool ReadAll( const wstring &path, string &out )
{
	HANDLE h = CreateFileW( path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL );
	if ( h == INVALID_HANDLE_VALUE )
		return false;
	LARGE_INTEGER size;
	GetFileSizeEx( h, &size );
	out.assign( (size_t)size.QuadPart, 0 );
	DWORD got = 0;
	bool ok = out.empty() || ( ReadFile( h, &out[0], (DWORD)out.size(), &got, NULL ) && got == out.size() );
	CloseHandle( h );
	return ok;
}

// Writes to a temp file and swaps it in, so a failed write never leaves a half-written file. Creates missing folders.
static bool WriteAll( const wstring &path, const string &data )
{
	SHCreateDirectoryExW( NULL, path.substr( 0, path.rfind( L'\\' ) ).c_str(), NULL );
	wstring tmp = path + L".tmp";
	HANDLE h = CreateFileW( tmp.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL );
	if ( h == INVALID_HANDLE_VALUE )
		return false;
	DWORD n = 0;
	bool ok = data.empty() || ( WriteFile( h, data.data(), (DWORD)data.size(), &n, NULL ) && n == data.size() );
	CloseHandle( h );
	ok = ok && MoveFileExW( tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING );
	if ( !ok )
		DeleteFileW( tmp.c_str() );
	return ok;
}

// .res files are UTF-8, ANSI or UTF-16LE with a BOM.
static wstring Decode( const string &b )
{
	if ( b.size() >= 2 && (BYTE)b[0] == 0xFF && (BYTE)b[1] == 0xFE )
		return wstring( (const wchar_t *)( b.data() + 2 ), ( b.size() - 2 ) / 2 );
	string s = b.compare( 0, 3, "\xEF\xBB\xBF" ) == 0 ? b.substr( 3 ) : b;
	if ( s.empty() || MultiByteToWideChar( CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), (int)s.size(), NULL, 0 ) )
		return Widen( s );
	return Widen( s, CP_ACP );
}

// Encodes like the file it replaces: UTF-16LE if it was, UTF-8 (keeping a BOM if it had one) otherwise.
static string Encode( const wstring &text, const string &old )
{
	if ( old.size() >= 2 && (BYTE)old[0] == 0xFF && (BYTE)old[1] == 0xFE )
		return string( "\xFF\xFE", 2 ) + string( (const char *)text.data(), text.size() * 2 );
	return ( old.compare( 0, 3, "\xEF\xBB\xBF" ) == 0 ? "\xEF\xBB\xBF" : "" ) + Utf8( text );
}

// Binary files the page makes (textures) come as base64.
static string Unbase64( const wstring &s )
{
	string out;
	unsigned bits = 0, n = 0;
	for ( wchar_t c : s )
	{
		int v = c >= 'A' && c <= 'Z' ? c - 'A' : c >= 'a' && c <= 'z' ? c - 'a' + 26 : c >= '0' && c <= '9' ? c - '0' + 52 : c == '+' ? 62 : c == '/' ? 63 : -1;
		if ( v < 0 ) // "=" padding
			continue;
		bits = bits << 6 | v, n += 6;
		if ( n >= 8 )
			n -= 8, out += (char)( bits >> n & 0xFF );
	}
	return out;
}

// Maps "resource/ClientScheme.res" to a path inside the HUD; refuses anything that could leave it.
static bool InHud( const wstring &rel, wstring &out )
{
	if ( g_hudPath.empty() || rel.empty() || rel.find( L".." ) != wstring::npos || rel.find( L':' ) != wstring::npos || rel[0] == L'/' || rel[0] == L'\\' )
		return false;
	out = g_hudPath + L"\\" + rel;
	for ( auto &c : out )
		if ( c == L'/' )
			c = L'\\';
	return true;
}

static bool IsDir( const wstring &p )
{
	DWORD a = GetFileAttributesW( p.c_str() );
	return a != INVALID_FILE_ATTRIBUTES && ( a & FILE_ATTRIBUTE_DIRECTORY );
}

static bool IsFile( const wstring &p )
{
	DWORD a = GetFileAttributesW( p.c_str() );
	return a != INVALID_FILE_ATTRIBUTES && !( a & FILE_ATTRIBUTE_DIRECTORY );
}

static bool IsHud( const wstring &dir )
{
	return IsDir( dir + L"\\resource" ) || IsDir( dir + L"\\scripts" );
}

// Calls fn( relative path ) for every file under dir. Doesn't follow junctions inside it.
template < typename Fn >
static void EachFile( const wstring &dir, const wstring &rel, Fn fn, int depth = 0 )
{
	WIN32_FIND_DATAW fd;
	HANDLE h = FindFirstFileW( ( dir + L"\\" + rel + L"*" ).c_str(), &fd );
	if ( h == INVALID_HANDLE_VALUE )
		return;
	do
	{
		wstring name = fd.cFileName;
		if ( name == L"." || name == L".." )
			continue;
		if ( !( fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ) )
			fn( rel + name );
		else if ( !( fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT ) && depth < 16 )
			EachFile( dir, rel + name + L"\\", fn, depth + 1 );
	} while ( FindNextFileW( h, &fd ) );
	FindClose( h );
}

// Changes are written straight into the HUD, so the game shows them. Before the first change to a file since the last
// save, its saved version goes to g_unsaved\files (or, for a file that didn't exist, a note in g_unsaved\new).
static void KeepSaved( const wstring &path )
{
	wstring rel = path.substr( g_hudPath.size() + 1 ), kept = g_unsaved + L"\\files\\" + rel, made = g_unsaved + L"\\new\\" + rel;
	if ( IsFile( kept ) || IsFile( made ) )
		return;
	WriteAll( g_unsaved + L"\\hud.txt", Utf8( g_hudPath ) );
	string data;
	if ( ReadAll( path, data ) )
		WriteAll( kept, data );
	else
		WriteAll( made, "" );
}

// Save HUD: the files as they are now become the saved ones. Returns how many changed.
static int Commit()
{
	int n = 0;
	EachFile( g_unsaved + L"\\files", L"", [&]( const wstring &rel ) {
		string was, now;
		ReadAll( g_unsaved + L"\\files\\" + rel, was );
		n += !ReadAll( g_hudPath + L"\\" + rel, now ) || now != was;
	} );
	EachFile( g_unsaved + L"\\new", L"", [&]( const wstring &rel ) { n += IsFile( g_hudPath + L"\\" + rel ); } );
	std::error_code ec;
	std::filesystem::remove_all( g_unsaved, ec );
	return n;
}

// Discard: puts the saved versions back into the HUD they came from (also after a crash) and removes new files.
// The saved versions are only let go once all of them are back.
static void Revert()
{
	string hud;
	if ( !ReadAll( g_unsaved + L"\\hud.txt", hud ) )
		return;
	wstring dir = Widen( hud );
	bool ok = true;
	EachFile( g_unsaved + L"\\files", L"", [&]( const wstring &rel ) {
		string data;
		ok = ReadAll( g_unsaved + L"\\files\\" + rel, data ) && WriteAll( dir + L"\\" + rel, data ) && ok;
	} );
	EachFile( g_unsaved + L"\\new", L"", [&]( const wstring &rel ) {
		DeleteFileW( ( dir + L"\\" + rel ).c_str() );
		// and the folders made for it, once empty (RemoveDirectory leaves any that still hold something)
		for ( size_t at = rel.rfind( L'\\' ); at != wstring::npos && at > 0; at = rel.rfind( L'\\', at - 1 ) )
			RemoveDirectoryW( ( dir + L"\\" + rel.substr( 0, at ) ).c_str() );
	} );
	std::error_code ec;
	if ( ok )
		std::filesystem::remove_all( g_unsaved, ec );
}

// Hint texts play ui/hint.wav on every update and the test server sends ten a second. Stopping the sound after it
// starts (what timer plugins do) depends on packet order and misses at high frame rates, so the plugin puts a folder
// with a silent one first in the game's search paths (see InstallPlugin).
static string SilentWav()
{
	const unsigned rate = 22050, bytes = 882; // 20 ms, 16-bit mono
	string w( 44 + bytes, '\0' );
	auto put = [&]( int at, unsigned v, int n ) { memcpy( &w[at], &v, n ); };
	memcpy( &w[0], "RIFF", 4 ), put( 4, 36 + bytes, 4 ), memcpy( &w[8], "WAVEfmt ", 8 );
	put( 16, 16, 4 ), put( 20, 1, 2 ), put( 22, 1, 2 ), put( 24, rate, 4 ), put( 28, rate * 2, 4 ), put( 32, 2, 2 ), put( 34, 16, 2 );
	memcpy( &w[36], "data", 4 ), put( 40, bytes, 4 );
	return w;
}

// Box corner textures for a HUD part's Texture1-4 (top-left, top-right, bottom-right, bottom-left: the order vgui's
// DrawBox uses): white inside the box and clear outside, tinted by the game with the box colour. The game draws a
// corner at a fixed size (8 px, or half the 8-unit HUD scale), so only its shape changes. u, v run from the outer
// corner (0, 0) to the box's inside (1, 1).
static bool CornerInside( const wstring &shape, double u, double v )
{
	if ( shape == L"square" ) // filled: a rounded box drawn with these is square
		return true;
	if ( shape == L"bevel" )
		return u + v >= 1;
	if ( shape == L"scoop" )
		return u * u + v * v >= 1;
	// "tight": a quarter circle of half the usual radius
	return u >= 0.5 || v >= 0.5 || ( u - 0.5 ) * ( u - 0.5 ) + ( v - 0.5 ) * ( v - 0.5 ) <= 0.25;
}

// VTF 7.2, BGRA8888, 64x64 with its mipmaps, plus the material. Returns the name for Texture1-4 without the number.
static wstring WriteCorners( const wstring &shape )
{
	const int S = 64, MIPS = 7, AA = 4;
	for ( int c = 1; c <= 4; ++c )
	{
		std::vector< std::vector< unsigned char > > mips( MIPS ); // alpha only, largest first
		mips[0].resize( S * S );
		for ( int y = 0; y < S; ++y )
		{
			for ( int x = 0; x < S; ++x )
			{
				int in = 0;
				for ( int sy = 0; sy < AA; ++sy )
				{
					for ( int sx = 0; sx < AA; ++sx )
					{
						double fx = ( x + ( sx + 0.5 ) / AA ) / S, fy = ( y + ( sy + 0.5 ) / AA ) / S;
						double u = c == 1 || c == 4 ? fx : 1 - fx, v = c <= 2 ? fy : 1 - fy;
						in += CornerInside( shape, u, v );
					}
				}
				mips[0][y * S + x] = (unsigned char)( in * 255 / ( AA * AA ) );
			}
		}
		for ( int m = 1; m < MIPS; ++m )
		{
			int w = S >> m, pw = w * 2;
			mips[m].resize( w * w );
			for ( int i = 0; i < w * w; ++i )
			{
				int x = i % w * 2, y = i / w * 2;
				const std::vector< unsigned char > &p = mips[m - 1];
				mips[m][i] = (unsigned char)( ( p[y * pw + x] + p[y * pw + x + 1] + p[( y + 1 ) * pw + x] + p[( y + 1 ) * pw + x + 1] + 2 ) / 4 );
			}
		}
		string vtf( 80, '\0' );
		auto put = [&]( int at, unsigned v, int n ) { memcpy( &vtf[at], &v, n ); };
		float one = 1.0f;
		memcpy( &vtf[0], "VTF", 4 ), put( 4, 7, 4 ), put( 8, 2, 4 ), put( 12, 80, 4 ), put( 16, S, 2 ), put( 18, S, 2 );
		put( 20, 0x4 | 0x8 | 0x2000, 4 ); // clamp S and T, 8-bit alpha
		put( 24, 1, 2 ), memcpy( &vtf[48], &one, 4 ), put( 52, 12, 4 ); // one frame, bumpmap scale, BGRA8888
		vtf[56] = MIPS, put( 57, 0xFFFFFFFF, 4 ), put( 63, 1, 2 ); // no low-res image, depth 1
		for ( int m = MIPS - 1; m >= 0; --m ) // smallest first
			for ( unsigned char a : mips[m] )
				vtf += string( 3, '\xff' ) + (char)a;
		wstring name = L"vgui\\hudeditor\\" + shape + L"_corner" + std::to_wstring( c );
		string vmt = "\"UnlitGeneric\"\r\n{\r\n\t\"$basetexture\" \"" + Utf8( name ) + "\"\r\n\t\"$translucent\" \"1\"\r\n\t\"$vertexcolor\" \"1\"\r\n"
			"\t\"$vertexalpha\" \"1\"\r\n\t\"$ignorez\" \"1\"\r\n\t\"$no_fullbright\" \"1\"\r\n}\r\n";
		std::replace( vmt.begin(), vmt.end(), '\\', '/' );
		wstring path = g_hudPath + L"\\materials\\" + name;
		KeepSaved( path + L".vtf" ), KeepSaved( path + L".vmt" );
		if ( !WriteAll( path + L".vtf", vtf ) || !WriteAll( path + L".vmt", vmt ) )
			return L"";
	}
	return L"vgui/hudeditor/" + shape + L"_corner";
}

// ---------- settings: recent HUD folders ----------
static std::vector< wstring > Recent()
{
	std::vector< wstring > out;
	string data;
	if ( !ReadAll( g_settings, data ) )
		return out;
	wstring all = Decode( data );
	for ( size_t a = 0, b; a < all.size(); a = b + 1 )
	{
		b = all.find( L'\n', a );
		if ( b == wstring::npos )
			b = all.size();
		wstring line = all.substr( a, b - a );
		if ( !line.empty() && line.back() == L'\r' )
			line.pop_back();
		if ( !line.empty() && IsHud( line ) )
			out.push_back( line );
	}
	return out;
}

static void RememberHud( const wstring &path )
{
	wstring text = path + L"\n";
	int n = 0;
	for ( auto &r : Recent() )
		if ( _wcsicmp( r.c_str(), path.c_str() ) && n++ < 9 )
			text += r + L"\n";
	WriteAll( g_settings, Utf8( text ) );
}

// ---------- game ----------
static wstring RegStr( const wchar_t *key, const wchar_t *value )
{
	wchar_t buf[1024];
	DWORD n = sizeof( buf );
	if ( RegGetValueW( HKEY_CURRENT_USER, key, value, RRF_RT_REG_SZ, NULL, buf, &n ) != ERROR_SUCCESS )
		return wstring();
	wstring s = buf;
	for ( auto &c : s )
		if ( c == L'/' )
			c = L'\\';
	return s;
}

// Looks for the game in every Steam library listed in libraryfolders.vdf.
static void FindGame()
{
	wstring steam = RegStr( L"Software\\Valve\\Steam", L"SteamPath" );
	g_steamExe = RegStr( L"Software\\Valve\\Steam", L"SteamExe" );
	if ( g_steamExe.empty() && !steam.empty() )
		g_steamExe = steam + L"\\steam.exe";

	std::vector< wstring > libs{ steam };
	string vdf;
	if ( ReadAll( steam + L"\\steamapps\\libraryfolders.vdf", vdf ) )
	{
		for ( size_t p = vdf.find( "\"path\"" ); p != string::npos; p = vdf.find( "\"path\"", p + 1 ) )
		{
			size_t a = vdf.find( '"', p + 6 ), b = vdf.find( '"', a + 1 );
			if ( a == string::npos || b == string::npos )
				break;
			string lib;
			for ( size_t i = a + 1; i < b; i++ )
				if ( !( vdf[i] == '\\' && vdf[i + 1] == '\\' ) )
					lib += vdf[i];
			libs.push_back( Widen( lib ) );
		}
	}
	for ( auto &lib : libs )
	{
		wstring g = lib + L"\\steamapps\\common\\Counter-Strike Source\\cstrike";
		if ( !lib.empty() && IsFile( g + L"\\gameinfo.txt" ) )
		{
			g_game = g;
			g_custom = g + L"\\custom";
			return;
		}
	}
}

static DWORD GamePid()
{
	HANDLE snap = CreateToolhelp32Snapshot( TH32CS_SNAPPROCESS, 0 );
	PROCESSENTRY32W e{ sizeof( e ) };
	DWORD pid = 0;
	for ( BOOL ok = Process32FirstW( snap, &e ); ok && !pid; ok = Process32NextW( snap, &e ) )
		if ( !_wcsicmp( e.szExeFile, L"cstrike_win64.exe" ) || !_wcsicmp( e.szExeFile, L"cstrike.exe" ) )
			pid = e.th32ProcessID;
	CloseHandle( snap );
	return pid;
}

static bool GameRunning()
{
	return GamePid() != 0;
}

// Started with the plugin (by the editor, with -insecure) rather than from Steam?
static bool PluginLoaded()
{
	HANDLE snap = CreateToolhelp32Snapshot( TH32CS_SNAPMODULE, GamePid() );
	if ( snap == INVALID_HANDLE_VALUE )
		return false;
	MODULEENTRY32W m{ sizeof( m ) };
	bool found = false;
	for ( BOOL ok = Module32FirstW( snap, &m ); ok && !found; ok = Module32NextW( snap, &m ) )
		found = !_wcsicmp( m.szModule, L"schemereload.dll" );
	CloseHandle( snap );
	return found;
}

// Copies the plugin into cstrike/addons unless the same files are already there, with the silent hint sound the
// plugin puts first in the game's search paths.
static wstring InstallPlugin()
{
	const wchar_t *files[][2] = { { L"PLUGIN_DLL", L"schemereload.dll" }, { L"PLUGIN_VDF", L"schemereload.vdf" }, { L"", L"schemereload_sounds\\sound\\ui\\hint.wav" } };
	for ( auto &f : files )
	{
		wstring path = g_game + L"\\addons\\" + f[1];
		string want = *f[0] ? Res( f[0] ) : SilentWav(), have;
		if ( ReadAll( path, have ) && have == want )
			continue;
		if ( !WriteAll( path, want ) )
			return L"Couldn't update " + path + L". Quit the game and try again.";
	}
	return wstring();
}

static void MountHud();

// Starts the game with the plugin. -insecure is for this launch only; it is never saved into the Steam launch options.
static wstring Launch()
{
	wstring err = InstallPlugin();
	if ( !err.empty() )
		return err;
	MountHud();
	DeleteFileW( ( g_game + L"\\addons\\schemereload_panels.txt" ).c_str() );
	ShellExecuteW( NULL, L"open", g_steamExe.c_str(), L"-applaunch 240 -insecure -windowed -noborder -novid -condebug", NULL, SW_SHOWNORMAL );
	return wstring();
}

// Console commands for the game: the plugin runs addons/schemereload_cmd.txt and deletes it.
static bool GameCommand( const wstring &cmd )
{
	HANDLE h = CreateFileW( ( g_game + L"\\addons\\schemereload_cmd.txt" ).c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_DELETE, NULL, OPEN_ALWAYS, 0, NULL );
	if ( h == INVALID_HANDLE_VALUE )
		return false;
	string s = Utf8( cmd ) + "\n";
	DWORD n = 0;
	bool ok = WriteFile( h, s.data(), (DWORD)s.size(), &n, NULL );
	CloseHandle( h );
	return ok;
}

// The game searches custom folders alphabetically and only finds the ones there when it started, so the plugin puts
// the HUD being edited first. A game that isn't running yet gets the command once the plugin loads.
static void MountHud()
{
	wstring p = g_hudPath;
	std::replace( p.begin(), p.end(), L'\\', L'/' );
	if ( !g_game.empty() && !p.empty() )
		GameCommand( L"schemereload_mount \"" + p + L"\"" );
}

// Stub scheme files that #base the game's defaults, for a brand-new HUD.
static bool CreateHud( const wstring &dir )
{
	auto stub = []( const string &base ) {
		return "#base \"" + base + "\"\n\n// Your changes go here. Anything not listed comes from " + base + ".\nScheme\n{\n}\n";
	};
	wstring r = dir + L"\\resource\\";
	return WriteAll( r + L"clientscheme_default.res", Res( L"CLIENT_DEF" ) ) && WriteAll( r + L"sourcescheme_default.res", Res( L"SOURCE_DEF" ) ) &&
		   WriteAll( r + L"ClientScheme.res", stub( "clientscheme_default.res" ) ) && WriteAll( r + L"SourceScheme.res", stub( "sourcescheme_default.res" ) );
}

static wstring PickFolder()
{
	ComPtr< IFileOpenDialog > dlg;
	if ( FAILED( CoCreateInstance( CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS( &dlg ) ) ) )
		return wstring();
	DWORD opts;
	dlg->GetOptions( &opts );
	dlg->SetOptions( opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM );
	dlg->SetTitle( L"Pick your HUD folder (the one with resource/ and scripts/ inside)" );
	ComPtr< IShellItem > item;
	wchar_t *path = NULL;
	if ( FAILED( dlg->Show( g_hwnd ) ) || FAILED( dlg->GetResult( &item ) ) || FAILED( item->GetDisplayName( SIGDN_FILESYSPATH, &path ) ) )
		return wstring();
	wstring out = path;
	CoTaskMemFree( path );
	return out;
}

static wstring PickFontFile()
{
	ComPtr< IFileOpenDialog > dlg;
	if ( FAILED( CoCreateInstance( CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS( &dlg ) ) ) )
		return wstring();
	COMDLG_FILTERSPEC filter = { L"Fonts (*.ttf, *.otf)", L"*.ttf;*.otf" };
	dlg->SetFileTypes( 1, &filter );
	dlg->SetTitle( L"Pick a font file to add to the HUD" );
	ComPtr< IShellItem > item;
	wchar_t *path = NULL;
	if ( FAILED( dlg->Show( g_hwnd ) ) || FAILED( dlg->GetResult( &item ) ) || FAILED( item->GetDisplayName( SIGDN_FILESYSPATH, &path ) ) )
		return wstring();
	wstring out = path;
	CoTaskMemFree( path );
	return out;
}

// The family name inside a .ttf/.otf (name table, name ID 1): the name the game's "name" key must use.
static wstring FontFamily( const string &d )
{
	auto u16 = [&]( size_t o ) -> unsigned { return o + 2 <= d.size() ? ( (BYTE)d[o] << 8 ) | (BYTE)d[o + 1] : 0; };
	auto u32 = [&]( size_t o ) -> size_t { return ( (size_t)u16( o ) << 16 ) | u16( o + 2 ); };
	for ( unsigned i = 0, n = u16( 4 ); i < n; ++i )
	{
		size_t rec = 12 + 16 * i;
		if ( d.compare( rec, 4, "name" ) )
			continue;
		size_t table = u32( rec + 8 ), strings = table + u16( table + 4 );
		wstring best;
		for ( unsigned j = 0, count = u16( table + 2 ); j < count; ++j )
		{
			size_t r = table + 6 + 12 * j;
			unsigned platform = u16( r ), lang = u16( r + 4 ), id = u16( r + 6 ), len = u16( r + 8 ), off = u16( r + 10 );
			if ( id != 1 || strings + off + len > d.size() )
				continue;
			if ( platform == 3 && ( best.empty() || lang == 0x409 ) )
			{
				best.clear();
				for ( unsigned k = 0; k + 1 < len; k += 2 )
					best += (wchar_t)u16( strings + off + k );
			}
			else if ( platform == 1 && best.empty() )
				best = Widen( d.substr( strings + off, len ), CP_ACP );
		}
		return best;
	}
	return wstring();
}

// The game's main window, found by its title.
static HWND GameWindow()
{
	HWND found = NULL;
	EnumWindows( []( HWND w, LPARAM p ) -> BOOL {
		wchar_t title[256];
		if ( IsWindowVisible( w ) && GetWindowTextW( w, title, 256 ) && !wcsncmp( title, GAME_TITLE, wcslen( GAME_TITLE ) ) )
			return *(HWND *)p = w, FALSE;
		return TRUE;
	}, (LPARAM)&found );
	return found;
}

// ---------- the preview: the game's window, captured here with Windows.Graphics.Capture ----------
// Aimed at the game's window itself (no picker, nothing else on screen is looked at). Frames arrive on a pool thread,
// at most about 40 a second, are halved when over 2000 px wide (the preview is never that big) and go into a buffer
// shared with the page: 16 bytes (frame count, width, height), then RGBA pixels. A new size needs a new buffer,
// made on the UI thread.
namespace wgc = winrt::Windows::Graphics::Capture;
static const UINT WM_CAPTURE_SIZE = WM_APP + 1, WM_CAPTURE_ENDED = WM_APP + 2;
static struct
{
	ComPtr< ID3D11Device > dev;
	ComPtr< ID3D11DeviceContext > ctx;
	ComPtr< ID3D11Texture2D > staging;
	winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice device{ nullptr };
	wgc::Direct3D11CaptureFramePool pool{ nullptr };
	wgc::GraphicsCaptureSession session{ nullptr };
	winrt::Windows::Graphics::SizeInt32 poolSize{};
	std::mutex lock; // the frame thread writing vs the UI thread swapping the buffer or stopping
	ComPtr< ICoreWebView2SharedBuffer > buf;
	BYTE *mem = NULL;
	UINT w = 0, h = 0, seq = 0, wantW = 0, wantH = 0;
	ULONGLONG last = 0;
} g_cap;

static void CopyFrame( wgc::Direct3D11CaptureFrame const &frame )
{
	auto size = frame.ContentSize();
	if ( size.Width != g_cap.poolSize.Width || size.Height != g_cap.poolSize.Height ) // the game changed resolution
	{
		g_cap.poolSize = size;
		g_cap.pool.Recreate( g_cap.device, winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, size );
		return;
	}
	ULONGLONG now = GetTickCount64();
	if ( now - g_cap.last < 25 )
		return;
	g_cap.last = now;
	UINT cw = size.Width, ch = size.Height, s = cw > 2000 ? 2 : 1, ow = cw / s, oh = ch / s;
	if ( !cw || !ch )
		return;
	if ( ow != g_cap.w || oh != g_cap.h || !g_cap.mem )
	{
		if ( ow != g_cap.wantW || oh != g_cap.wantH )
		{
			g_cap.wantW = ow, g_cap.wantH = oh;
			PostMessageW( g_hwnd, WM_CAPTURE_SIZE, 0, 0 );
		}
		return;
	}
	ComPtr< ID3D11Texture2D > tex;
	auto access = frame.Surface().as< ::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess >();
	if ( FAILED( access->GetInterface( IID_PPV_ARGS( &tex ) ) ) )
		return;
	D3D11_TEXTURE2D_DESC d = {};
	if ( g_cap.staging )
		g_cap.staging->GetDesc( &d );
	if ( !g_cap.staging || d.Width != cw || d.Height != ch )
	{
		d = {};
		d.Width = cw, d.Height = ch, d.MipLevels = d.ArraySize = 1, d.Format = DXGI_FORMAT_B8G8R8A8_UNORM, d.SampleDesc.Count = 1;
		d.Usage = D3D11_USAGE_STAGING, d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		g_cap.staging.Reset();
		if ( FAILED( g_cap.dev->CreateTexture2D( &d, NULL, &g_cap.staging ) ) )
			return;
	}
	D3D11_BOX box = { 0, 0, 0, cw, ch, 1 };
	g_cap.ctx->CopySubresourceRegion( g_cap.staging.Get(), 0, 0, 0, 0, tex.Get(), 0, &box );
	D3D11_MAPPED_SUBRESOURCE m;
	if ( FAILED( g_cap.ctx->Map( g_cap.staging.Get(), 0, D3D11_MAP_READ, 0, &m ) ) )
		return;
	BYTE *out = g_cap.mem + 16;
	for ( UINT y = 0; y < oh; ++y )
	{
		const BYTE *a = (const BYTE *)m.pData + (size_t)y * s * m.RowPitch, *b = a + ( s - 1 ) * m.RowPitch;
		for ( UINT x = 0; x < ow; ++x, out += 4 )
		{
			// BGRA in, RGBA out; halved, the average of each 2x2 (at full size the same pixel four times)
			const BYTE *p = a + x * s * 4, *q = b + x * s * 4, *p2 = p + ( s - 1 ) * 4, *q2 = q + ( s - 1 ) * 4;
			for ( int c = 0; c < 3; ++c )
				out[c] = (BYTE)( ( p[2 - c] + p2[2 - c] + q[2 - c] + q2[2 - c] + 2 ) / 4 );
			out[3] = 255;
		}
	}
	g_cap.ctx->Unmap( g_cap.staging.Get(), 0 );
	UINT *head = (UINT *)g_cap.mem;
	head[1] = ow, head[2] = oh, head[0] = ++g_cap.seq;
}

// UI thread: a buffer of the size the frames need, handed to the page (which lets its old one go)
static void MakeFrameBuffer()
{
	ComPtr< ICoreWebView2Environment12 > env;
	ComPtr< ICoreWebView2_17 > web;
	ComPtr< ICoreWebView2SharedBuffer > buf;
	BYTE *mem = NULL;
	UINT w, h;
	{
		std::lock_guard< std::mutex > hold( g_cap.lock );
		w = g_cap.wantW, h = g_cap.wantH;
	}
	if ( !w || !h || FAILED( g_env.As( &env ) ) || FAILED( g_web.As( &web ) ) || FAILED( env->CreateSharedBuffer( 16 + (UINT64)w * h * 4, &buf ) ) || FAILED( buf->get_Buffer( &mem ) ) )
		return;
	{
		std::lock_guard< std::mutex > hold( g_cap.lock );
		g_cap.buf = buf, g_cap.mem = mem, g_cap.w = w, g_cap.h = h;
	}
	web->PostSharedBufferToScript( buf.Get(), COREWEBVIEW2_SHARED_BUFFER_ACCESS_READ_ONLY, NULL );
}

static void StopCapture()
{
	wgc::GraphicsCaptureSession session{ nullptr };
	wgc::Direct3D11CaptureFramePool pool{ nullptr };
	{
		std::lock_guard< std::mutex > hold( g_cap.lock );
		std::swap( session, g_cap.session );
		std::swap( pool, g_cap.pool );
		g_cap.buf.Reset(), g_cap.mem = NULL, g_cap.w = g_cap.h = g_cap.wantW = g_cap.wantH = 0;
	}
	// closed outside the lock: closing can wait for a frame being copied, which takes it
	if ( session )
		session.Close();
	if ( pool )
		pool.Close();
}

static wstring StartCapture()
{
	if ( g_cap.session )
		return L"";
	HWND game = GameWindow();
	if ( !game )
		return L"no game window";
	if ( IsIconic( game ) ) // a minimised window sends no frames: restore it without taking the front
		ShowWindowAsync( game, SW_SHOWNOACTIVATE );
	try
	{
		if ( !g_cap.dev )
		{
			winrt::check_hresult( D3D11CreateDevice( NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT, NULL, 0, D3D11_SDK_VERSION, &g_cap.dev, NULL, &g_cap.ctx ) );
			ComPtr< IDXGIDevice > dxgi;
			winrt::check_hresult( g_cap.dev.As( &dxgi ) );
			winrt::com_ptr< ::IInspectable > device;
			winrt::check_hresult( CreateDirect3D11DeviceFromDXGIDevice( dxgi.Get(), device.put() ) );
			g_cap.device = device.as< winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice >();
		}
		auto interop = winrt::get_activation_factory< wgc::GraphicsCaptureItem, IGraphicsCaptureItemInterop >();
		wgc::GraphicsCaptureItem item{ nullptr };
		winrt::check_hresult( interop->CreateForWindow( game, winrt::guid_of< wgc::GraphicsCaptureItem >(), winrt::put_abi( item ) ) );
		std::lock_guard< std::mutex > hold( g_cap.lock );
		g_cap.poolSize = item.Size();
		g_cap.pool = wgc::Direct3D11CaptureFramePool::CreateFreeThreaded( g_cap.device, winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, g_cap.poolSize );
		g_cap.pool.FrameArrived( []( wgc::Direct3D11CaptureFramePool const &pool, auto && ) {
			std::lock_guard< std::mutex > hold( g_cap.lock );
			if ( auto frame = pool.TryGetNextFrame() )
			{
				if ( g_cap.pool == pool )
					CopyFrame( frame );
				frame.Close();
			}
		} );
		item.Closed( []( auto &&, auto && ) { PostMessageW( g_hwnd, WM_CAPTURE_ENDED, 0, 0 ); } );
		g_cap.session = g_cap.pool.CreateCaptureSession( item );
		g_cap.session.IsCursorCaptureEnabled( false );
		try { g_cap.session.IsBorderRequired( false ); } catch ( ... ) {} // no yellow frame round the game (Windows 11)
		g_cap.session.StartCapture();
	}
	catch ( winrt::hresult_error const &e )
	{
		StopCapture();
		return L"Couldn't capture the game window: " + wstring( e.message() );
	}
	return L"";
}

// A key sent to the game brings it to the front, and in a map the game then locks the mouse inside its window. Once
// the key is let go, the editor takes the front back. Windows only lets the program the user last typed into change
// the front window, so this borrows the game's input state for the call.
static UINT g_keyVk;
static void BringEditorBack()
{
	HWND fg = GetForegroundWindow();
	DWORD fgThread = fg ? GetWindowThreadProcessId( fg, NULL ) : 0, me = GetCurrentThreadId();
	bool attach = fgThread && fgThread != me && AttachThreadInput( me, fgThread, TRUE );
	SetForegroundWindow( g_hwnd );
	if ( attach )
		AttachThreadInput( me, fgThread, FALSE );
	if ( g_ctl )
		g_ctl->MoveFocus( COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC );
}

static wstring NameOf( const wstring &path )
{
	return path.substr( path.find_last_of( L"\\/" ) + 1 );
}

// ---------- messages from the page: "id\nop\narg\nbody", answered with "id\n1\nresult" or "id\n0\nerror" ----------
static void OnMessage( const wstring &msg )
{
	size_t a = msg.find( L'\n' ), b = msg.find( L'\n', a + 1 ), c = msg.find( L'\n', b + 1 );
	if ( a == wstring::npos || b == wstring::npos || c == wstring::npos )
		return;
	wstring id = msg.substr( 0, a ), op = msg.substr( a + 1, b - a - 1 ), arg = msg.substr( b + 1, c - b - 1 ), body = msg.substr( c + 1 );
	bool ok = true;
	wstring out, path;
	auto fail = [&]( const wstring &err ) { ok = false; out = err; };
	const wstring noGame = L"Counter-Strike: Source wasn't found in your Steam libraries.";

	if ( op == L"start" ) // on page load: "running", "launched", or why neither
	{
		auto recent = Recent();
		if ( g_hudPath.empty() && !recent.empty() )
			g_hudPath = recent[0];
		if ( g_game.empty() )
			fail( noGame );
		else if ( GameRunning() )
			out = PluginLoaded() ? L"running" : L"foreign";
		else if ( ( out = Launch() ).size() )
			ok = false;
		else
			out = L"launched";
	}
	else if ( op == L"launch" )
	{
		if ( g_game.empty() )
			fail( noGame );
		else if ( GameRunning() )
			fail( L"The game is already running. Quit it first." );
		else if ( ( out = Launch() ).size() )
			ok = false;
	}
	else if ( op == L"huds" ) // "label\tpath" per line: recent HUDs, then HUD folders in custom
	{
		std::vector< wstring > seen;
		auto add = [&]( const wstring &p, const wstring &where ) {
			for ( auto &s : seen )
				if ( !_wcsicmp( s.c_str(), p.c_str() ) )
					return;
			seen.push_back( p );
			out += NameOf( p ) + L"\t" + p + L"\t" + where + L"\n";
		};
		for ( auto &r : Recent() )
			add( r, L"recent" );
		WIN32_FIND_DATAW fd;
		HANDLE h = FindFirstFileW( ( g_custom + L"\\*" ).c_str(), &fd );
		for ( BOOL more = h != INVALID_HANDLE_VALUE; more; more = FindNextFileW( h, &fd ) )
		{
			wstring p = g_custom + L"\\" + fd.cFileName;
			if ( fd.cFileName[0] != L'.' && IsHud( p ) )
				add( p, L"custom" );
		}
		if ( h != INVALID_HANDLE_VALUE )
			FindClose( h );
		out += L"\tcurrent\t" + g_hudPath;
	}
	else if ( op == L"browse" )
	{
		if ( ( out = PickFolder() ).empty() )
			fail( L"cancelled" );
	}
	else if ( op == L"new" ) // arg = name: a new HUD in the custom folder
	{
		path = g_custom + L"\\" + arg;
		if ( g_custom.empty() )
			fail( noGame );
		else if ( arg.empty() || arg.find_first_of( L"\\/:*?\"<>|" ) != wstring::npos || arg.find( L".." ) != wstring::npos )
			fail( L"Use a plain folder name (no \\ / : * ? \" < > |)." );
		else if ( IsDir( path ) )
			fail( arg + L" is already in your custom folder." );
		else if ( !CreateHud( path ) )
			fail( L"Couldn't create " + path );
		else
			out = path;
	}
	else if ( op == L"open" ) // arg = HUD folder; unsaved changes to the one open now are discarded (the page asks first)
	{
		if ( !IsHud( arg ) )
			fail( arg + L" doesn't look like a HUD (no resource or scripts folder)." );
		else
		{
			Revert();
			g_hudPath = arg;
			RememberHud( arg );
			MountHud();
			out = NameOf( arg );
		}
	}
	else if ( op == L"read" )
	{
		string data;
		if ( !InHud( arg, path ) )
			fail( L"bad path" );
		else if ( !ReadAll( path, data ) )
			fail( L"missing" );
		else
			out = Decode( data );
	}
	else if ( op == L"write" )
	{
		string old;
		if ( !InHud( arg, path ) )
			fail( L"bad path" );
		else
		{
			ReadAll( path, old );
			KeepSaved( path );
			if ( !WriteAll( path, Encode( body, old ) ) )
				fail( L"couldn't write " + path );
		}
	}
	else if ( op == L"writebin" ) // arg = path in the HUD, body = the file in base64
	{
		if ( !InHud( arg, path ) )
			fail( L"bad path" );
		else if ( KeepSaved( path ), !WriteAll( path, Unbase64( body ) ) )
			fail( L"couldn't write " + path );
	}
	else if ( op == L"save" ) // keep the changes: answers how many files changed
	{
		if ( g_hudPath.empty() )
			fail( L"No HUD open." );
		else
			out = std::to_wstring( Commit() );
	}
	else if ( op == L"fontfaces" ) // installed font families, for the font name suggestions
	{
		std::vector< wstring > names;
		LOGFONTW lf{};
		lf.lfCharSet = DEFAULT_CHARSET;
		HDC dc = GetDC( NULL );
		EnumFontFamiliesExW( dc, &lf, []( const LOGFONTW *f, const TEXTMETRICW *, DWORD, LPARAM p ) -> int {
			auto &v = *(std::vector< wstring > *)p;
			if ( f->lfFaceName[0] != L'@' && ( v.empty() || v.back() != f->lfFaceName ) )
				v.push_back( f->lfFaceName );
			return 1;
		}, (LPARAM)&names, 0 );
		ReleaseDC( NULL, dc );
		std::sort( names.begin(), names.end() );
		names.erase( std::unique( names.begin(), names.end() ), names.end() );
		for ( auto &n : names )
			out += ( out.empty() ? L"" : L"\n" ) + n;
	}
	else if ( op == L"defaultlayout" ) // the game's own HudLayout.res, for HUDs that don't have one yet
		out = Decode( Res( L"HUDLAYOUT_DEF" ) );
	else if ( op == L"config" ) // the player's saved game settings (scoreboard row colours are cvars, not HUD files)
	{
		string data;
		if ( g_game.empty() || !ReadAll( g_game + L"\\cfg\\config.cfg", data ) )
			fail( L"no config.cfg" );
		else
			out = Decode( data );
	}
	else if ( op == L"vanilla" ) // arg = a window .res or chatscheme.res by name: the game's own copy, for HUDs without one
	{
		wstring name = L"V_" + NameOf( arg );
		string data = Res( name.substr( 0, name.rfind( L'.' ) ).c_str() );
		if ( data.empty() )
			fail( L"missing" );
		else
			out = Decode( data );
	}
	else if ( op == L"corners" ) // box corner textures in a shape: "vgui/hudeditor/<shape>_corner" (+ 1-4)
	{
		if ( arg != L"bevel" && arg != L"scoop" && arg != L"tight" && arg != L"square" )
			fail( L"unknown corner shape" );
		else if ( ( out = WriteCorners( arg ) ).empty() )
			fail( L"couldn't write the corner textures" );
	}
	else if ( op == L"panels" ) // what the plugin says is on screen right now
	{
		string data;
		if ( g_game.empty() || !GameRunning() || !ReadAll( g_game + L"\\addons\\schemereload_panels.txt", data ) )
			fail( L"game not running with the plugin" );
		else
			out = Decode( data );
	}
	else if ( op == L"addfont" ) // copy a font file into the HUD: "resource/fonts/x.ttf\tFamily"
	{
		wstring src = PickFontFile(), dest = g_hudPath + L"\\resource\\fonts\\" + NameOf( src );
		string data;
		if ( src.empty() )
			fail( L"cancelled" );
		else if ( g_hudPath.empty() || !ReadAll( src, data ) || ( KeepSaved( dest ), !WriteAll( dest, data ) ) )
			fail( L"Couldn't copy " + src );
		else
		{
			wstring family = FontFamily( data );
			out = L"resource/fonts/" + NameOf( src ) + L"\t" + ( family.empty() ? NameOf( src ).substr( 0, NameOf( src ).rfind( L'.' ) ) : family );
		}
	}
	else if ( op == L"hudfonts" ) // family names of the font files in the HUD
	{
		EachFile( g_hudPath, L"", [&]( const wstring &rel ) {
			wstring ext = rel.size() > 4 ? rel.substr( rel.size() - 4 ) : L"";
			string data;
			if ( ( !_wcsicmp( ext.c_str(), L".ttf" ) || !_wcsicmp( ext.c_str(), L".otf" ) ) && ReadAll( g_hudPath + L"\\" + rel, data ) )
			{
				wstring family = FontFamily( data );
				if ( !family.empty() )
					out += ( out.empty() ? L"" : L"\n" ) + family;
			}
		} );
	}
	else if ( op == L"capture" ) // start sending the game's window to the page (see StartCapture)
	{
		wstring err = StartCapture();
		if ( !err.empty() )
			fail( err );
	}
	else if ( op == L"key" ) // arg = scan code (hex), then " down" or " up" for half a press: bring the game to the front and press the key there
	{
		// a key held over the preview (Tab for the scoreboard) goes down only; its release reaches the game by itself
		// once the game is in front, or comes here as " up" if it happened first
		bool down = arg.find( L" up" ) == wstring::npos, up = arg.find( L" down" ) == wstring::npos;
		HWND w = GameWindow();
		if ( !w )
			fail( L"no game window" );
		else
		{
			// the game drops keys that arrive while it is still activating: wait until it is in front, then a little more
			if ( GetForegroundWindow() != w )
			{
				SetForegroundWindow( w );
				for ( int i = 0; i < 50 && GetForegroundWindow() != w; ++i )
					Sleep( 10 );
				Sleep( 250 );
			}
			INPUT in[2] = {};
			in[0].type = in[1].type = INPUT_KEYBOARD;
			in[0].ki.wScan = in[1].ki.wScan = (WORD)wcstoul( arg.c_str(), NULL, 16 );
			in[0].ki.dwFlags = KEYEVENTF_SCANCODE; // the game reads scan codes, not virtual keys
			in[1].ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;
			UINT vk = MapVirtualKeyW( in[0].ki.wScan, MAPVK_VSC_TO_VK );
			if ( down && !up && vk && ( GetAsyncKeyState( vk ) & 0x8000 ) )
			{
				// still held: Windows would pass the game this press as a repeat (the key is already down), which the
				// game ignores, so release it first; the real release reaches the game later, as it is now in front
				std::swap( in[0], in[1] );
				SendInput( 2, in, sizeof( INPUT ) );
			}
			else if ( down && !up ) // let go while the game was coming to the front: that release went nowhere
				SendInput( 2, in, sizeof( INPUT ) );
			else
				SendInput( down + up, in + !down, sizeof( INPUT ) );
			if ( down && vk )
			{
				g_keyVk = vk; // once it is let go, the editor comes back to the front (see WM_TIMER)
				SetTimer( g_hwnd, 3, 100, NULL );
			}
		}
	}
	else if ( op == L"cmd" ) // console commands for the game
	{
		if ( g_game.empty() || !GameCommand( body ) )
			fail( L"couldn't reach the game" );
	}
	else if ( op == L"explore" )
	{
		if ( !g_hudPath.empty() )
			ShellExecuteW( NULL, L"open", g_hudPath.c_str(), NULL, NULL, SW_SHOWNORMAL );
	}
	else if ( op == L"quit" )
	{
		DestroyWindow( g_hwnd );
		return;
	}
	else if ( op == L"done" ) // --selftest result
	{
		WriteAll( g_selftestOut, Utf8( body ) );
		g_exitCode = body == L"ok" ? 0 : 1;
		DestroyWindow( g_hwnd );
		return;
	}
	else
		fail( L"unknown op " + op );

	wstring reply = id + ( ok ? L"\n1\n" : L"\n0\n" ) + out;
	g_web->PostWebMessageAsString( reply.c_str() );
}

// ---------- window + WebView2 ----------
static void Fit()
{
	RECT r;
	GetClientRect( g_hwnd, &r );
	if ( g_ctl )
		g_ctl->put_Bounds( r );
}

static HRESULT OnController( HRESULT hr, ICoreWebView2Controller *ctl )
{
	if ( FAILED( hr ) || !ctl )
		return MessageBoxW( g_hwnd, L"Couldn't start the WebView2 browser view.", L"CS:S HUD Editor", MB_ICONERROR ), PostQuitMessage( 1 ), S_OK;
	g_ctl = ctl;
	g_ctl->get_CoreWebView2( &g_web );
	Fit();

	// The page is served from inside the exe at https://hud.editor/.
	g_web->AddWebResourceRequestedFilter( L"https://hud.editor/*", COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL );
	g_web->add_WebResourceRequested( Callback< ICoreWebView2WebResourceRequestedEventHandler >(
		[]( ICoreWebView2 *, ICoreWebView2WebResourceRequestedEventArgs *args ) -> HRESULT {
			string html = Res( L"EDITOR" );
			ComPtr< IStream > stream;
			stream.Attach( SHCreateMemStream( (const BYTE *)html.data(), (UINT)html.size() ) );
			ComPtr< ICoreWebView2WebResourceResponse > resp;
			g_env->CreateWebResourceResponse( stream.Get(), 200, L"OK", L"Content-Type: text/html; charset=utf-8", &resp );
			return args->put_Response( resp.Get() );
		} ).Get(), NULL );

	g_web->add_WebMessageReceived( Callback< ICoreWebView2WebMessageReceivedEventHandler >(
		[]( ICoreWebView2 *, ICoreWebView2WebMessageReceivedEventArgs *args ) -> HRESULT {
			LPWSTR s = NULL;
			if ( SUCCEEDED( args->TryGetWebMessageAsString( &s ) ) && s )
			{
				OnMessage( s );
				CoTaskMemFree( s );
			}
			return S_OK;
		} ).Get(), NULL );

	return g_web->Navigate( g_selftestOut.empty() ? L"https://hud.editor/" : L"https://hud.editor/#hosttest" );
}

static HRESULT OnEnvironment( HRESULT hr, ICoreWebView2Environment *env )
{
	if ( FAILED( hr ) || !env )
	{
		if ( MessageBoxW( g_hwnd, L"This needs the Microsoft Edge WebView2 Runtime (built into Windows 11).\n\nOpen the download page?", L"CS:S HUD Editor", MB_ICONERROR | MB_YESNO ) == IDYES )
			ShellExecuteW( NULL, L"open", L"https://developer.microsoft.com/microsoft-edge/webview2/", NULL, NULL, SW_SHOWNORMAL );
		PostQuitMessage( 1 );
		return S_OK;
	}
	g_env = env;
	return env->CreateCoreWebView2Controller( g_hwnd, Callback< ICoreWebView2CreateCoreWebView2ControllerCompletedHandler >( OnController ).Get() );
}

static LRESULT CALLBACK WndProc( HWND h, UINT m, WPARAM w, LPARAM l )
{
	static DWORD s_closeAsked;
	switch ( m )
	{
	case WM_SIZE:
		Fit();
		return 0;
	case WM_MOVE: // WebView2 places its popups (colour pickers, drop-down lists) from where it thinks the window is
		if ( g_ctl )
			g_ctl->NotifyParentWindowPositionChanged();
		return 0;
	case WM_CLOSE: // let the page ask about unsaved changes; a second click within 5 s closes anyway
		if ( g_web && g_selftestOut.empty() && GetTickCount() - s_closeAsked > 5000 )
		{
			s_closeAsked = GetTickCount();
			g_web->PostWebMessageAsString( L"!close" );
			return 0;
		}
		break;
	case WM_CAPTURE_SIZE:
		MakeFrameBuffer();
		return 0;
	case WM_CAPTURE_ENDED: // the game window closed
		StopCapture();
		if ( g_web )
			g_web->PostWebMessageAsString( L"!captureended" );
		return 0;
	case WM_TIMER:
		if ( w == 3 )
		{
			if ( !( GetAsyncKeyState( g_keyVk ) & 0x8000 ) )
			{
				KillTimer( h, 3 );
				BringEditorBack();
			}
			return 0;
		}
		// --selftest took too long
		WriteAll( g_selftestOut, "timeout" );
		g_exitCode = 1;
		DestroyWindow( h );
		return 0;
	case WM_DESTROY:
		StopCapture();
		Revert(); // unsaved changes (the page asked about them first)
		PostQuitMessage( g_exitCode );
		return 0;
	}
	return DefWindowProcW( h, m, w, l );
}

int WINAPI wWinMain( HINSTANCE inst, HINSTANCE, LPWSTR, int show )
{
	SetProcessDpiAwarenessContext( DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 );
	CoInitializeEx( NULL, COINIT_APARTMENTTHREADED );

	wchar_t *known = NULL;
	SHGetKnownFolderPath( FOLDERID_LocalAppData, 0, NULL, &known );
	wstring data = wstring( known ) + L"\\CSSHudEditor";
	CoTaskMemFree( known );
	g_settings = data + L"\\recent.txt";
	g_unsaved = data + L"\\unsaved";

	// CSSHudEditor.exe --selftest <empty folder>: runs the page's checks with that folder standing in for the game
	int argc = 0;
	LPWSTR *argv = CommandLineToArgvW( GetCommandLineW(), &argc );
	if ( argc == 3 && !wcscmp( argv[1], L"--selftest" ) )
	{
		g_custom = argv[2];
		g_selftestOut = g_custom + L"\\selftest.txt";
		g_settings = g_custom + L"\\recent.txt";
		g_unsaved = g_custom + L"\\unsaved";
	}
	else
		FindGame();
	Revert(); // left by an editor that didn't close normally

	WNDCLASSW wc{};
	wc.lpfnWndProc = WndProc;
	wc.hInstance = inst;
	wc.hCursor = LoadCursor( NULL, IDC_ARROW );
	wc.hbrBackground = CreateSolidBrush( RGB( 0x16, 0x18, 0x1c ) );
	wc.lpszClassName = L"CSSHudEditor";
	RegisterClassW( &wc );
	g_hwnd = CreateWindowW( wc.lpszClassName, L"CS:S HUD Editor", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1500, 900, NULL, NULL, inst, NULL );
	if ( g_selftestOut.empty() )
		ShowWindow( g_hwnd, show );
	else
		SetTimer( g_hwnd, 1, 30000, NULL );

	HRESULT hr = CreateCoreWebView2EnvironmentWithOptions( NULL, data.c_str(), NULL, Callback< ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler >( OnEnvironment ).Get() );
	if ( FAILED( hr ) )
		OnEnvironment( hr, NULL );

	MSG msg;
	while ( GetMessageW( &msg, NULL, 0, 0 ) > 0 )
	{
		TranslateMessage( &msg );
		DispatchMessageW( &msg );
	}
	return (int)msg.wParam;
}
