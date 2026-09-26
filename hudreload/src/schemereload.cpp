// schemereload - Counter-Strike: Source (x64) server plugin that applies changes to
// SourceScheme.res, ClientScheme.res and ChatScheme.res while the game is running.
//
// The game reads scheme files once at startup and keeps them in memory. On reload we:
//   1. parse the files again (same search paths and #base handling as the game),
//   2. write every resolved colour into the live scheme's colour table, which the
//      engine checks before BaseSettings when a panel asks for a colour,
//   3. merge changed font/border settings and rebuild them,
//   4. send "reloadscheme" to every panel (Panel::OnCommand -> InvalidateLayout(false, true)),
//      then hud_reloadscheme when in a map.
// Each step is crash-protected on its own: if one faults it is switched off and the rest keep working.
//
// Needs the game to be started with -insecure (plugins don't load otherwise).

#include "interface.h"
#include "filesystem.h"
#include "engine/iserverplugin.h"
#include "eiface.h"
#include "cdll_int.h"
#include "ienginevgui.h"
#include "tier1/tier1.h"
#include "tier1/convar.h"
#include "tier1/KeyValues.h"
#include "tier1/utlbuffer.h"
#include "tier1/bitbuf.h"
#include "irecipientfilter.h"
#include <math.h>
#include "vgui/IScheme.h"
#include "vgui/IBorder.h"
#include "vstdlib/IKeyValuesSystem.h"
#include <limits.h>
#include "vgui/IPanel.h"
#include "vgui/ISurface.h"
#include "vgui/IVGui.h"
#include "dt_send.h"
#include "server_class.h"
#include "iservernetworkable.h"
#include "iserverunknown.h"
#include "igameevents.h"
#include "Color.h"
#include <stdio.h>
#include <unordered_map>

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

extern "C" bool SR_StartTimer( void ( *fn )(), unsigned ms );
extern "C" void SR_StopTimer();
extern "C" unsigned long long SR_NewestResTime( const char *filePath );
extern "C" unsigned long long SR_FileTime( const char *path );
extern "C" int SR_SafeCall( void ( *fn )() );
extern "C" bool SR_WriteFileAtomic( const char *path, const char *data, int len );

using namespace vgui;

static IFileSystem *g_pFS;
static IVEngineServer *g_pEngineServer;
static IVEngineClient *g_pEngineClient;
static IEngineVGui *g_pEngineVGui;
static ISchemeManager *g_pSchemeMgr;
static IPanel *g_pVPanel;
static IVGui *g_pVGui;
static ISurface *g_pSurface;
static bool g_bLevelActive;
static bool g_bDisabled;

static ConVar scheme_reload_watch( "scheme_reload_watch", "1", 0, "1 = reload automatically when a .res file next to SourceScheme/ClientScheme is saved" );
static ConVar scheme_reload_fonts( "scheme_reload_fonts", "1", 0, "1 = also apply font changes on reload (experimental)" );

// vgui_controls module names; IPanel::GetPanel only returns a Panel * when given the right one
static const char *s_Modules[] = { "ClientDLL", "GameUI", "ServerBrowser", "BaseUI", "ENGINE" };

// The game can load the same file more than once (one copy per sizing panel), so a file
// maps to every in-memory copy that matches it.
static const int MAX_COPIES = 8;

struct SchemeFile
{
	const char *relPath;
	const char *label;
	const char *tags[3];
	HScheme handles[MAX_COPIES];
	int nHandles;
	char fullPath[1024];
	unsigned long long lastTime;

	bool Owns( HScheme h ) const
	{
		for ( int i = 0; i < nHandles; ++i )
		{
			if ( handles[i] == h )
				return true;
		}
		return false;
	}
};

static SchemeFile g_Files[] =
{
	{ "resource/SourceScheme.res", "SourceScheme", { "Tracker", "SourceScheme", NULL } },
	{ "resource/ClientScheme.res", "ClientScheme", { "ClientScheme", "HudScheme", NULL } },
	{ "resource/ChatScheme.res", "ChatScheme", { "ChatScheme", NULL } },
};
static const int NUM_FILES = ARRAYSIZE( g_Files );

//-----------------------------------------------------------------------------
// Parsing
//-----------------------------------------------------------------------------
// The game loads schemes from the GAME search path (custom/* first); searching every path
// instead would find platform/resource/SourceScheme.res first.
static const char *SchemePathID( const char *relPath )
{
	return g_pFS->FileExists( relPath, "GAME" ) ? "GAME" : NULL;
}

static KeyValues *LoadFresh( const char *relPath )
{
	KeyValues *kv = new KeyValues( "Scheme" );
	if ( !kv->LoadFromFile( g_pFS, relPath, SchemePathID( relPath ) ) )
	{
		kv->deleteThis();
		return NULL;
	}
	return kv;
}

static bool FullPathOf( const char *relPath, char *out, int size )
{
	if ( g_pFS->RelativePathToFullPath( relPath, SchemePathID( relPath ), out, size ) )
		return true;
	out[0] = 0;
	return false;
}

static bool ParseColor( const char *s, Color &out )
{
	int r, g, b, a = 255;
	if ( !s || sscanf( s, "%d %d %d %d", &r, &g, &b, &a ) < 3 )
		return false;
	out.SetColor( r, g, b, a );
	return true;
}

static bool IsColor( const char *s )
{
	Color c;
	return ParseColor( s, c );
}

// Same lookup order as the engine: a literal colour, then Colors, then BaseSettings (recursively).
static const char *ResolveFresh( KeyValues *colors, KeyValues *base, const char *name, int depth = 0 )
{
	if ( !name || depth > 16 )
		return NULL;
	if ( IsColor( name ) )
		return name;
	const char *s = colors ? colors->GetString( name, NULL ) : NULL;
	if ( s )
		return s;
	s = base ? base->GetString( name, NULL ) : NULL;
	if ( s )
		return ResolveFresh( colors, base, s, depth + 1 );
	return NULL;
}

//-----------------------------------------------------------------------------
// Panels
//-----------------------------------------------------------------------------
static VPANEL TopPanel()
{
	VPANEL p = g_pEngineVGui->GetPanel( PANEL_ROOT );
	while ( p && g_pVPanel->GetParent( p ) )
		p = g_pVPanel->GetParent( p );
	return p;
}

static int PanelModule( VPANEL p )
{
	for ( int i = 0; i < ARRAYSIZE( s_Modules ); ++i )
	{
		if ( g_pVPanel->GetPanel( p, s_Modules[i] ) )
			return i;
	}
	return -1;
}

static void CollectPanelSchemes( VPANEL p, CUtlVector< HScheme > &out, int depth )
{
	if ( !p || depth > 64 )
		return;
	HScheme h = g_pVPanel->GetScheme( p );
	if ( h && out.Find( h ) == -1 )
		out.AddToTail( h );
	for ( int i = 0; i < g_pVPanel->GetChildCount( p ); ++i )
		CollectPanelSchemes( g_pVPanel->GetChild( p, i ), out, depth + 1 );
}

//-----------------------------------------------------------------------------
// Finding the live schemes in memory
//-----------------------------------------------------------------------------

// How well a live scheme matches a freshly parsed file: +2 for every colour the game currently
// returns with the same value, -1 for every one it doesn't. Uses the game's own lookup, so it
// works however the file's sections are ordered.
static int ScoreScheme( HScheme h, KeyValues *fresh )
{
	IScheme *s = g_pSchemeMgr->GetIScheme( h );
	if ( !s )
		return -100000;
	KeyValues *colors = fresh->FindKey( "Colors" );
	KeyValues *base = fresh->FindKey( "BaseSettings" );
	KeyValues *sections[2] = { colors, base };
	const Color missing( 1, 2, 3, 4 );
	int score = 0;
	for ( int sec = 0; sec < 2; ++sec )
	{
		if ( !sections[sec] )
			continue;
		for ( KeyValues *k = sections[sec]->GetFirstValue(); k; k = k->GetNextValue() )
		{
			Color want;
			if ( !ParseColor( ResolveFresh( colors, base, k->GetName() ), want ) )
				continue;
			score += ( s->GetColor( k->GetName(), missing ) == want ) ? 2 : -1;
		}
	}
	return score;
}

static void GatherCandidates( SchemeFile &f, CUtlVector< HScheme > &candidates )
{
	for ( int i = 0; f.tags[i]; ++i )
	{
		HScheme h = g_pSchemeMgr->GetScheme( f.tags[i] );
		if ( h && candidates.Find( h ) == -1 )
			candidates.AddToTail( h );
	}
	HScheme hDefault = g_pSchemeMgr->GetDefaultScheme();
	if ( hDefault && candidates.Find( hDefault ) == -1 )
		candidates.AddToTail( hDefault );
	CollectPanelSchemes( TopPanel(), candidates, 0 );
}

static bool IsTaken( SchemeFile &f, HScheme h )
{
	for ( int j = 0; j < NUM_FILES; ++j )
	{
		if ( &g_Files[j] != &f && g_Files[j].Owns( h ) )
			return true;
	}
	return false;
}

// Picks every candidate scoring within 10% of the best one: identical copies score the same.
static void MapScheme( SchemeFile &f, KeyValues *fresh )
{
	CUtlVector< HScheme > candidates;
	GatherCandidates( f, candidates );

	CUtlVector< int > scores;
	int bestScore = 0;
	for ( int i = 0; i < candidates.Count(); ++i )
	{
		int score = IsTaken( f, candidates[i] ) ? -100000 : ScoreScheme( candidates[i], fresh );
		scores.AddToTail( score );
		if ( score > bestScore )
			bestScore = score;
	}
	if ( bestScore <= 0 )
		return;

	char list[256] = "";
	for ( int i = 0; i < candidates.Count() && f.nHandles < MAX_COPIES; ++i )
	{
		if ( scores[i] <= 0 || scores[i] * 10 < bestScore * 9 )
			continue;
		f.handles[f.nHandles++] = candidates[i];
		Q_snprintf( list + Q_strlen( list ), sizeof( list ) - Q_strlen( list ), "%s%d", list[0] ? ", " : "", (int)candidates[i] );
	}
	Msg( "[schemereload] found %s in memory (scheme %s)\n", f.label, list );
}

static void EnsureMapped()
{
	static double s_nextTry;
	double now = Plat_FloatTime();
	if ( now < s_nextTry )
		return;
	s_nextTry = now + 2.0;

	for ( int i = 0; i < NUM_FILES; ++i )
	{
		SchemeFile &f = g_Files[i];
		if ( f.nHandles )
			continue;
		KeyValues *fresh = LoadFresh( f.relPath );
		if ( fresh )
		{
			MapScheme( f, fresh );
			fresh->deleteThis();
		}
	}
}

//-----------------------------------------------------------------------------
// Applying
//-----------------------------------------------------------------------------
static int PatchColors( KeyValues *live, KeyValues *fresh )
{
	KeyValues *colors = fresh->FindKey( "Colors" );
	KeyValues *base = fresh->FindKey( "BaseSettings" );
	KeyValues *sections[2] = { colors, base };
	int changed = 0;
	for ( int s = 0; s < 2; ++s )
	{
		if ( !sections[s] )
			continue;
		for ( KeyValues *k = sections[s]->GetFirstValue(); k; k = k->GetNextValue() )
		{
			const char *val = ResolveFresh( colors, base, k->GetName() );
			if ( !IsColor( val ) )
				continue;
			const char *cur = live->GetString( k->GetName(), NULL );
			if ( cur && !Q_strcmp( cur, val ) )
				continue;
			live->SetString( k->GetName(), val );
			++changed;
		}
	}
	return changed;
}

static KeyValues *FindPeer( KeyValues *start, const char *name )
{
	for ( KeyValues *k = start; k; k = k->GetNextKey() )
	{
		if ( !Q_stricmp( k->GetName(), name ) )
			return k;
	}
	return NULL;
}

// pruneFrom: the depth from which blocks missing from the file are removed too (a border's sides and lines);
// above it they stay, since fonts and borders can't be deleted from a running game
static int MergeInto( KeyValues *live, KeyValues *fresh, int pruneFrom = INT_MAX, int depth = 0 )
{
	int changed = 0;
	for ( KeyValues *k = fresh->GetFirstSubKey(); k; k = k->GetNextKey() )
	{
		if ( k->GetDataType() == KeyValues::TYPE_NONE )
		{
			changed += MergeInto( live->FindKey( k->GetName(), true ), k, pruneFrom, depth + 1 );
			continue;
		}
		const char *v = k->GetString();
		const char *cur = live->GetString( k->GetName(), NULL );
		if ( cur && !Q_strcmp( cur, v ) )
			continue;
		live->SetString( k->GetName(), v );
		++changed;
	}
	// values taken out of the file go back to unset (e.g. "outline" removed = no outline)
	for ( KeyValues *k = live->GetFirstSubKey(), *next; k; k = next )
	{
		next = k->GetNextKey();
		if ( ( k->GetDataType() == KeyValues::TYPE_NONE && depth < pruneFrom ) || fresh->FindKey( k->GetName() ) )
			continue;
		live->RemoveSubKey( k );
		k->deleteThis();
		++changed;
	}
	return changed;
}

// Sends "reloadscheme" to the top panel of each module; Panel::InvalidateLayout(false, true)
// then re-applies scheme settings down through that module's children by itself.
// Panels outside the known modules (e.g. the surface's own top panel) are skipped: they
// aren't vgui_controls Panels and don't handle the command. Each send is crash-protected
// on its own so one misbehaving panel can't stop the rest.
static int g_nRefreshed, g_nRefreshFailed;
static VPANEL g_SendTarget;

static void SendReloadScheme()
{
	KeyValues *msg = new KeyValues( "Command", "command", "reloadscheme" );
	g_pVPanel->SendMessage( g_SendTarget, msg, g_SendTarget );
	msg->deleteThis();
}

static void RefreshPanels( VPANEL p, int parentModule, int depth )
{
	if ( !p || depth > 64 )
		return;
	int module = PanelModule( p );
	// BaseUI/ENGINE (last two modules) are the engine's own panels; staticPanel crashes on the command
	if ( module >= 0 && module < ARRAYSIZE( s_Modules ) - 2 && module != parentModule )
	{
		g_SendTarget = p;
		if ( SR_SafeCall( SendReloadScheme ) )
			++g_nRefreshed;
		else if ( ++g_nRefreshFailed <= 5 )
			Warning( "[schemereload] panel '%s' (%s) crashed while refreshing, skipped\n", g_pVPanel->GetName( p ), s_Modules[module] );
	}
	for ( int i = 0; i < g_pVPanel->GetChildCount( p ); ++i )
		RefreshPanels( g_pVPanel->GetChild( p, i ), module, depth + 1 );
}

//-----------------------------------------------------------------------------
// Reload, one crash-protected step at a time
//-----------------------------------------------------------------------------
enum
{
	STEP_READ,
	STEP_COLORS,
	STEP_FONTS,
	STEP_BORDERS,
	STEP_PANELS,
	STEP_HUD,
	STEP_COUNT
};
static const char *s_StepNames[STEP_COUNT] = { "read files", "colours", "fonts", "borders", "refresh panels", "hud_reloadscheme" };
static bool g_bStepBroken[STEP_COUNT];

static KeyValues *g_pFresh[NUM_FILES];
static KeyValues *g_pLive[NUM_FILES][MAX_COPIES];	// each copy's colour table
static KeyValues *g_pRoot[NUM_FILES][MAX_COPIES];	// each copy's whole file (NULL if not found)
static int g_nLive[NUM_FILES];

// IScheme only hands out the colour table. The whole file (CScheme::m_pData) sits two pointers before it in the
// CScheme object (m_pData, m_pkvBaseSettings, m_pkvColors); confirmed by asking it for Colors. Sections come in
// file order after #base merging, so Fonts can sit before Colors and isn't reachable from the colour table.
static KeyValues *SchemeRoot( IScheme *s, const KeyValues *colors )
{
	void **p = (void **)s;
	for ( int i = 2; i < 64; ++i )
	{
		if ( p[i] != colors )
			continue;
		KeyValues *root = (KeyValues *)p[i - 2];
		return root && root->FindKey( "Colors" ) == colors ? root : NULL;
	}
	return NULL;
}
static int g_nColors, g_nFonts, g_nBorders;

static void StepRead()
{
	for ( int i = 0; i < NUM_FILES; ++i )
	{
		SchemeFile &f = g_Files[i];
		g_nLive[i] = 0;
		g_pFresh[i] = LoadFresh( f.relPath );
		if ( !g_pFresh[i] )
		{
			Warning( "[schemereload] couldn't read %s (missing file or syntax error?)\n", f.relPath );
			continue;
		}
		if ( !f.nHandles )
			MapScheme( f, g_pFresh[i] );
		for ( int c = 0; c < f.nHandles; ++c )
		{
			IScheme *s = g_pSchemeMgr->GetIScheme( f.handles[c] );
			KeyValues *live = s ? const_cast< KeyValues * >( s->GetColorData() ) : NULL;
			if ( live )
			{
				g_pRoot[i][g_nLive[i]] = SchemeRoot( s, live );
				g_pLive[i][g_nLive[i]++] = live;
			}
		}
		if ( !g_nLive[i] )
			Warning( "[schemereload] %s not found in memory, skipped (see scheme_reload_status)\n", f.label );
	}
}

// BaseSettings values that aren't colours (MainMenu.MenuItemHeight, FrameTitleBar.Font...): the game reads them as
// strings from the scheme's BaseSettings section, so set them there.
static int PatchSettings( KeyValues *root, KeyValues *fresh )
{
	KeyValues *live = root ? root->FindKey( "BaseSettings" ) : NULL, *base = fresh->FindKey( "BaseSettings" );
	int changed = 0;
	for ( KeyValues *k = base && live ? base->GetFirstValue() : NULL; k; k = k->GetNextValue() )
	{
		const char *val = k->GetString();
		if ( IsColor( ResolveFresh( fresh->FindKey( "Colors" ), base, k->GetName() ) ) )
			continue;
		const char *cur = live->GetString( k->GetName(), NULL );
		if ( cur && !Q_strcmp( cur, val ) )
			continue;
		live->SetString( k->GetName(), val );
		++changed;
	}
	return changed;
}

// Counts are taken from the first copy only, so they read as "values changed in the file".
static void StepColors()
{
	for ( int i = 0; i < NUM_FILES; ++i )
	{
		for ( int c = 0; g_pFresh[i] && c < g_nLive[i]; ++c )
		{
			int n = PatchColors( g_pLive[i][c], g_pFresh[i] ) + PatchSettings( g_pRoot[i][c], g_pFresh[i] );
			if ( c == 0 )
				g_nColors += n;
		}
	}
}

static int MergeSection( int i, const char *section, int pruneFrom = INT_MAX )
{
	int first = 0;
	for ( int c = 0; g_pFresh[i] && c < g_nLive[i]; ++c )
	{
		KeyValues *live = g_pRoot[i][c] ? g_pRoot[i][c]->FindKey( section ) : FindPeer( g_pLive[i][c], section );
		KeyValues *fresh = g_pFresh[i]->FindKey( section );
		if ( !live || !fresh )
			continue;
		int n = MergeInto( live, fresh, pruneFrom );
		if ( c == 0 )
			first = n;
	}
	return first;
}

// .ttf files listed in CustomFontFiles are registered when a scheme loads; register ones added since.
static CUtlStringList g_FontFiles;
static int RegisterNewFontFiles()
{
	int added = 0; // the engine ignores files it already has; the first reload counts every file once (one extra font rebuild)
	for ( int i = 0; g_pSurface && i < NUM_FILES; ++i )
	{
		KeyValues *list = g_pFresh[i] ? g_pFresh[i]->FindKey( "CustomFontFiles" ) : NULL;
		for ( KeyValues *k = list ? list->GetFirstSubKey() : NULL; k; k = k->GetNextKey() )
		{
			const char *file = k->GetFirstSubKey() ? k->GetString( "font", NULL ) : k->GetString();
			bool known = false;
			for ( int j = 0; file && j < g_FontFiles.Count() && !known; ++j )
				known = !Q_stricmp( g_FontFiles[j], file );
			if ( !file || !*file || known )
				continue;
			g_FontFiles.CopyAndAddToTail( file );
			if ( g_pSurface->AddCustomFontFile( k->GetString( "name", NULL ), file ) )
				++added;
		}
	}
	return added;
}

static void StepFonts()
{
	if ( !scheme_reload_fonts.GetBool() )
		return;
	int added = RegisterNewFontFiles();
	for ( int i = 0; i < NUM_FILES; ++i )
		g_nFonts += MergeSection( i, "Fonts" );
	if ( g_nFonts || added )
	{
		g_pSchemeMgr->ReloadFonts();
		// the fonts now use the new face, but text is drawn from glyphs cached from the old one; drop them
		if ( g_pSurface )
			g_pSurface->ResetFontCaches();
	}
}

//-----------------------------------------------------------------------------
// Borders the game didn't make at startup. vgui makes one border object per "Borders" block when a scheme
// loads and never again, and UpdateBorders only re-reads those; a block added later, or a name pointed at a
// different border, would only show after a restart. The plugin draws those itself, the same way as vgui's
// line border (Border.cpp): each side is a list of 1-pixel lines, outside in, each with a colour and offsets.
//-----------------------------------------------------------------------------
class CEditorBorder : public IBorder
{
public:
	CEditorBorder( IScheme *scheme, const char *name ) : m_pScheme( scheme ), m_eBackground( BACKGROUND_FILLED )
	{
		Q_strncpy( m_szName, name, sizeof( m_szName ) );
		memset( m_Inset, 0, sizeof( m_Inset ) );
	}
	IScheme *m_pScheme;

	virtual void Paint( VPANEL panel )
	{
		int w, h;
		g_pVPanel->GetSize( panel, w, h );
		Paint( 0, 0, w, h, -1, 0, 0 );
	}
	virtual void Paint( int x, int y, int wide, int tall ) { Paint( x, y, wide, tall, -1, 0, 0 ); }
	virtual void Paint( int x, int y, int wide, int tall, int, int, int ) // no gaps for group box titles
	{
		for ( int s = 0; s < 4; ++s )
		{
			for ( int i = 0; i < m_Lines[s].Count(); ++i )
			{
				const Line &l = m_Lines[s][i];
				g_pSurface->DrawSetColor( l.col );
				if ( s == SIDE_LEFT )		g_pSurface->DrawFilledRect( x + i, y + l.start, x + i + 1, tall - l.end );
				else if ( s == SIDE_TOP )	g_pSurface->DrawFilledRect( x + l.start, y + i, wide - l.end, y + i + 1 );
				else if ( s == SIDE_RIGHT )	g_pSurface->DrawFilledRect( wide - i - 1, y + l.start, wide - i, tall - l.end );
				else						g_pSurface->DrawFilledRect( x + l.start, tall - i - 1, wide - l.end, tall - i );
			}
		}
	}
	virtual void SetInset( int l, int t, int r, int b ) { m_Inset[0] = l; m_Inset[1] = t; m_Inset[2] = r; m_Inset[3] = b; }
	virtual void GetInset( int &l, int &t, int &r, int &b ) { l = m_Inset[0]; t = m_Inset[1]; r = m_Inset[2]; b = m_Inset[3]; }
	virtual void ApplySchemeSettings( IScheme *scheme, KeyValues *data )
	{
		static const char *s_Sides[4] = { "Left", "Top", "Right", "Bottom" }; // SIDE_LEFT.. order
		memset( m_Inset, 0, sizeof( m_Inset ) );
		sscanf( data->GetString( "inset", "0 0 0 0" ), "%d %d %d %d", &m_Inset[0], &m_Inset[1], &m_Inset[2], &m_Inset[3] );
		for ( int s = 0; s < 4; ++s )
		{
			m_Lines[s].RemoveAll();
			KeyValues *side = data->FindKey( s_Sides[s] );
			for ( KeyValues *k = side ? side->GetFirstSubKey() : NULL; k; k = k->GetNextKey() )
			{
				Line l;
				l.col = scheme->GetColor( k->GetString( "color", "" ), Color( 0, 0, 0, 0 ) );
				l.start = l.end = 0;
				sscanf( k->GetString( "offset", "0 0" ), "%d %d", &l.start, &l.end );
				m_Lines[s].AddToTail( l );
			}
		}
		m_eBackground = (backgroundtype_e)data->GetInt( "backgroundtype" );
	}
	virtual const char *GetName() { return m_szName; }
	virtual void SetName( const char *name ) { Q_strncpy( m_szName, name, sizeof( m_szName ) ); }
	virtual backgroundtype_e GetBackgroundType() { return m_eBackground; }
	virtual bool PaintFirst() { return false; }

private:
	struct Line { Color col; int start, end; };
	CUtlVector< Line > m_Lines[4];
	int m_Inset[4];
	backgroundtype_e m_eBackground;
	char m_szName[64];
};

// CScheme's border list: each border name's symbol and the border it gives (vgui2 Scheme.cpp m_BorderList). It sits
// just after the colour table and a colour count in the object; used only if it matches what the scheme reports.
struct SchemeBorder_t
{
	IBorder *border;
	int borderSymbol;
	bool bSharedBorder;	// true: vgui won't delete it (another entry, or the plugin, owns it)
};
typedef CUtlVector< SchemeBorder_t > BorderList_t;

static BorderList_t *SchemeBorderList( IScheme *s, const KeyValues *colors )
{
	void **p = (void **)s;
	int n = s->GetBorderCount();
	for ( int i = 2; n > 0 && i < 64; ++i )
	{
		if ( p[i] != colors )
			continue;
		for ( int j = i + 1; j < i + 4; ++j )
		{
			BorderList_t *list = (BorderList_t *)&p[j];
			if ( list->Count() != n || !list->Base() )
				continue;
			bool ok = true, named = false;
			for ( int k = 0; ok && k < n; ++k )
			{
				const SchemeBorder_t &e = list->Element( k );
				ok = e.border == s->GetBorderAtIndex( k );
				if ( ok && !e.bSharedBorder && e.border )
					named |= e.borderSymbol == KeyValuesSystem()->GetSymbolForString( e.border->GetName() );
			}
			if ( ok && named )
				return list;
		}
		return NULL;
	}
	return NULL;
}

static CUtlVector< CEditorBorder * > g_EditorBorders;	// kept for the session; listed as shared, so vgui never deletes them
static bool g_bBorderListWarned;

// Points each name in the file's Borders section at the right border: the game's own object when it made one
// for that block (UpdateBorders has re-read it), else one of ours; names that refer to another border follow it.
static void InstallBorders( IScheme *s, const KeyValues *colors, KeyValues *fresh )
{
	BorderList_t *list = SchemeBorderList( s, colors );
	if ( !list )
	{
		if ( !g_bBorderListWarned )
			Warning( "[schemereload] can't reach the border list: new or renamed borders show after a restart\n" );
		g_bBorderListWarned = true;
		return;
	}
	for ( int pass = 0; pass < 2; ++pass ) // blocks first, then the names that point at them
	{
		for ( KeyValues *k = fresh->GetFirstSubKey(); k; k = k->GetNextKey() )
		{
			bool block = k->GetDataType() == KeyValues::TYPE_NONE;
			if ( block != ( pass == 0 ) )
				continue;
			int sym = KeyValuesSystem()->GetSymbolForString( k->GetName() ), at = -1;
			for ( int n = 0; n < list->Count() && at < 0; ++n )
			{
				if ( list->Element( n ).borderSymbol == sym )
					at = n;
			}
			IBorder *want;
			if ( block )
			{
				if ( at >= 0 && !list->Element( at ).bSharedBorder )
					continue;
				CEditorBorder *b = NULL;
				for ( int n = 0; n < g_EditorBorders.Count() && !b; ++n )
				{
					if ( g_EditorBorders[n]->m_pScheme == s && !Q_stricmp( g_EditorBorders[n]->GetName(), k->GetName() ) )
						b = g_EditorBorders[n];
				}
				if ( !b )
					b = g_EditorBorders[g_EditorBorders.AddToTail( new CEditorBorder( s, k->GetName() ) )];
				b->ApplySchemeSettings( s, k );
				want = b;
			}
			else
				want = s->GetBorder( k->GetString() );
			if ( at < 0 )
			{
				SchemeBorder_t e = { want, sym, true };
				list->AddToTail( e );
			}
			else if ( list->Element( at ).border != want )
			{
				list->Element( at ).border = want;
				list->Element( at ).bSharedBorder = true; // its old border now leaks rather than being deleted twice
			}
		}
	}
}

static void StepBorders()
{
	for ( int i = 0; i < NUM_FILES; ++i )
		g_nBorders += MergeSection( i, "Borders", 1 );
	if ( !g_nColors && !g_nBorders )
		return;
	g_pSchemeMgr->UpdateBorders();
	for ( int i = 0; i < NUM_FILES; ++i )
	{
		KeyValues *fresh = g_pFresh[i] ? g_pFresh[i]->FindKey( "Borders" ) : NULL;
		for ( int c = 0; fresh && c < g_nLive[i]; ++c )
		{
			IScheme *s = g_pSchemeMgr->GetIScheme( g_Files[i].handles[c] );
			if ( s )
				InstallBorders( s, g_pLive[i][c], fresh );
		}
	}
}

// Before the plugin unloads: nothing may keep pointing at its borders.
static void RemoveEditorBorders()
{
	for ( int i = 0; i < NUM_FILES; ++i )
	{
		for ( int c = 0; c < g_Files[i].nHandles; ++c )
		{
			IScheme *s = g_pSchemeMgr->GetIScheme( g_Files[i].handles[c] );
			BorderList_t *list = s ? SchemeBorderList( s, s->GetColorData() ) : NULL;
			for ( int n = 0; list && n < list->Count(); ++n )
			{
				for ( int e = 0; e < g_EditorBorders.Count(); ++e )
				{
					if ( list->Element( n ).border == g_EditorBorders[e] )
						list->Element( n ).border = s->GetBorder( "" ); // the base border
				}
			}
		}
	}
}

// Re-applying a scheme makes dialogs re-apply their .res files, which sets hidden tab pages
// visible again. Record visibility first and put it back afterwards.
static void RecordVisibility( VPANEL p, std::unordered_map< VPANEL, bool > &was, int depth )
{
	if ( !p || depth > 64 )
		return;
	was[p] = g_pVPanel->IsVisible( p );
	for ( int i = 0; i < g_pVPanel->GetChildCount( p ); ++i )
		RecordVisibility( g_pVPanel->GetChild( p, i ), was, depth + 1 );
}

// Puts back what was shown or hidden before, walking the tree as it is now: the refresh can delete panels (a class
// menu button remakes its info page), so a recorded one may be gone.
static CUtlVector< VPANEL > g_Reshow; // visible before the last reload, see Reshow
static void RestoreVisibility( VPANEL p, const std::unordered_map< VPANEL, bool > &was, int depth )
{
	for ( int i = 0; p && depth < 64 && i < g_pVPanel->GetChildCount( p ); ++i )
	{
		VPANEL c = g_pVPanel->GetChild( p, i );
		auto it = was.find( c );
		if ( c && it != was.end() )
		{
			if ( g_pVPanel->IsVisible( c ) != it->second )
				g_pVPanel->SetVisible( c, it->second );
			if ( it->second )
				g_Reshow.AddToTail( c );
		}
		RestoreVisibility( c, was, depth + 1 );
	}
}

static bool RunStep( int step, void ( *fn )() );
static void StepHud();
static void HideRowTemplates();
static double g_flReshowUntil;

static VPANEL FindNamed( VPANEL p, const char *name, int depth )
{
	if ( !p || depth > 64 )
		return 0;
	if ( !Q_stricmp( g_pVPanel->GetName( p ), name ) )
		return p;
	for ( int i = 0; i < g_pVPanel->GetChildCount( p ); ++i )
		if ( VPANEL f = FindNamed( g_pVPanel->GetChild( p, i ), name, depth + 1 ) )
			return f;
	return 0;
}

// For testing without the mouse: a button acts as if clicked (vgui's "PressButton").
CON_COMMAND( schemereload_press, "schemereload_press <panel name>: clicks that button" )
{
	VPANEL p = args.ArgC() > 1 && g_pVGui ? FindNamed( TopPanel(), args.Arg( 1 ), 0 ) : 0;
	if ( p )
		g_pVGui->PostMessage( p, new KeyValues( "PressButton" ), 0 );
	else
		Msg( "[schemereload] no panel named %s\n", args.Arg( 1 ) );
}

// A reload knocks the main menu about. The logo (GameMenuButton, GameMenuButton2) is left see-through, and the menu
// only fades it back in when a dialog opens or closes. The items get their own defaults (MenuLarge, 12 pixels) in place
// of the larger font and item height the menu gave them at startup. So keep what they had. Label::SetFont/GetFont,
// Panel::SetInfo and Menu::Set/GetMenuItemHeight are called through the vtable. The slots come from vgui_slots.cpp and
// are checked against the one GameUI.dll itself calls (SetFont, 0x710) before anything is used.
int SlotLabelSetFont(), SlotLabelGetFont(), SlotPanelSetInfo(), SlotMenuSetItemHeight(), SlotMenuGetItemHeight();
struct KeptMenu { VPANEL p; HFont font; float alpha; int itemTall; };
static CUtlVector< KeptMenu > g_KeptMenu;
typedef HFont ( *GetFontFn )( void * );
typedef void ( *SetFontFn )( void *, HFont );
typedef bool ( *SetInfoFn )( void *, KeyValues * );
typedef int ( *GetIntFn )( void * );
typedef void ( *SetIntFn )( void *, int );

static float PanelAlpha( VPANEL p )
{
	KeyValues *q = new KeyValues( "alpha" );
	float a = g_pVPanel->RequestInfo( p, q ) ? q->GetFloat( "alpha", -1 ) : -1;
	q->deleteThis();
	return a;
}

static void KeepMenu()
{
	g_KeptMenu.RemoveAll();
	static bool s_bChecked, s_bSlotsOk;
	if ( !s_bChecked )
	{
		s_bChecked = true;
		s_bSlotsOk = SlotLabelSetFont() == 0x710 / 8 && SlotLabelGetFont() == SlotLabelSetFont() + 1 && SlotPanelSetInfo() > 0 &&
			SlotMenuSetItemHeight() > 0 && SlotMenuGetItemHeight() > 0;
		if ( !s_bSlotsOk )
			Warning( "[schemereload] this game's menu panels don't match; a reload may hide the main menu logo until a dialog opens (%d %d %d %d %d)\n",
				SlotLabelSetFont(), SlotLabelGetFont(), SlotPanelSetInfo(), SlotMenuSetItemHeight(), SlotMenuGetItemHeight() );
	}
	VPANEL base = s_bSlotsOk ? FindNamed( TopPanel(), "BaseGameUIPanel", 0 ) : 0;
	for ( int i = 0; base && i < g_pVPanel->GetChildCount( base ); ++i )
	{
		VPANEL c = g_pVPanel->GetChild( base, i );
		bool logo = !Q_strnicmp( g_pVPanel->GetName( c ), "GameMenuButton", 14 );
		if ( !logo && Q_stricmp( g_pVPanel->GetName( c ), "GameMenu" ) )
			continue;
		for ( int j = -1; j < ( logo ? 0 : g_pVPanel->GetChildCount( c ) ); ++j )
		{
			VPANEL p = j < 0 ? c : g_pVPanel->GetChild( c, j );
			void *panel = g_pVPanel->GetPanel( p, "GameUI" );
			bool menu = !logo && j < 0;
			if ( !panel || ( !logo && !menu && Q_stricmp( g_pVPanel->GetClassName( p ), "CGameMenuItem" ) ) )
				continue;
			void **vt = *(void ***)panel;
			KeptMenu k = { p, 0, PanelAlpha( p ), -1 };
			if ( menu )
				k.itemTall = ( (GetIntFn)vt[SlotMenuGetItemHeight()] )( panel );
			else
				k.font = ( (GetFontFn)vt[SlotLabelGetFont()] )( panel );
			g_KeptMenu.AddToTail( k );
		}
	}
}

static void RestoreMenu()
{
	for ( int i = 0; i < g_KeptMenu.Count(); ++i )
	{
		const KeptMenu &k = g_KeptMenu[i];
		void *panel = g_pVPanel->GetPanel( k.p, "GameUI" );
		if ( !panel )
			continue;
		void **vt = *(void ***)panel;
		if ( k.itemTall > 0 && k.itemTall < 1000 )
		{
			// the refresh set MainMenu.MenuItemHeight as it is in the file; at startup the menu scales it to the screen
			// (640x480 units), so do that, which also picks up a changed height
			int now = ( (GetIntFn)vt[SlotMenuGetItemHeight()] )( panel ), w = 0, h = 0;
			g_pSurface->GetScreenSize( w, h );
			int tall = now > 0 && now != k.itemTall && h > 0 ? now * h / 480 : k.itemTall;
			( (SetIntFn)vt[SlotMenuSetItemHeight()] )( panel, tall );
			KeyValues *msg = new KeyValues( "Command", "command", "performlayout" );
			g_pVPanel->SendMessage( k.p, msg, k.p );
			msg->deleteThis();
		}
		if ( k.font )
			( (SetFontFn)vt[SlotLabelSetFont()] )( panel, k.font );
		if ( k.alpha >= 0 && PanelAlpha( k.p ) != k.alpha )
		{
			KeyValues *kv = new KeyValues( "alpha" );
			kv->SetFloat( "alpha", k.alpha );
			( (SetInfoFn)vt[SlotPanelSetInfo()] )( panel, kv );
			kv->deleteThis();
		}
	}
	g_KeptMenu.RemoveAll();
}

// Windows the game lays out once, when it makes them (team and class menus, the MOTD): give each of their controls its
// block of the .res again, so a change (own colours, places, sizes) shows without a restart. Panel::ApplySettings
// through the vtable, on controls that exist: EditablePanel::LoadControlSettings would delete and remake the ones the
// file made, which the game still points at (that crashed the next map load). Texts are left as the game set them
// (the MOTD's title). Only when the menu check above found this game's slots as expected.
int SlotApplySettings();
typedef void ( *ApplySettingsFn )( void *, KeyValues * );
static void *g_ApplyPanel;
static KeyValues *g_ApplyKeys;
static int g_ApplySlot;
static void ApplyOne()
{
	( (ApplySettingsFn)( *(void ***)g_ApplyPanel )[g_ApplySlot] )( g_ApplyPanel, g_ApplyKeys );
}
static CUtlStringList g_ApplyBroken; // "window/block" whose settings crashed once: left alone until a restart

// Controls the editor adds to these windows (blocks named hudeditor..., e.g. command buttons): when the game has none by
// that name, made the way the game makes a .res file's controls (BuildGroup::NewControl): the window's control factory,
// parented to the window, which also gets a button's commands (team and class select run them). ApplySettings then
// names and places it.
int SlotCreateControlByName();
int SlotSetParent();
int SlotAddActionSignalTarget();
typedef void *( *CreateControlFn )( void *, const char * );
typedef void ( *PanelVPanelFn )( void *, VPANEL );
static void *g_MakeIn, *g_Made;
static VPANEL g_MakeInV;
static const char *g_MakeClass;
static void MakeOne()
{
	g_Made = ( (CreateControlFn)( *(void ***)g_MakeIn )[SlotCreateControlByName()] )( g_MakeIn, g_MakeClass );
	if ( !g_Made )
		return;
	void **vt = *(void ***)g_Made;
	( (PanelVPanelFn)vt[SlotSetParent()] )( g_Made, g_MakeInV );
	( (PanelVPanelFn)vt[SlotAddActionSignalTarget()] )( g_Made, g_MakeInV );
}
static bool IsEditorBlock( const char *name ) { return !Q_strnicmp( name, "hudeditor", 9 ); }

static void ReapplyLayouts()
{
	// (the round-end and killer panels are HUD parts that read their .res once too; the killer panel's parts are blocks
	// inside FreezePanelBG, which applies them to its own children)
	static const char *s_Res[][2] = { { "team", "Resource/UI/TeamMenu.res" }, { "class_ct", "Resource/UI/ClassMenu_CT.res" },
		{ "class_ter", "Resource/UI/ClassMenu_TER.res" }, { "info", "Resource/UI/TextWindow.res" },
		{ "WinPanel_Round", "Resource/UI/Win_Round.res" }, { "FreezePanel", "Resource/UI/FreezePanel_Basic.res" } };
	int slot = g_ApplySlot = SlotApplySettings();
	if ( slot <= 0 || SlotLabelSetFont() != 0x710 / 8 )
		return;
	bool canMake = SlotCreateControlByName() > 0 && SlotSetParent() > 0 && SlotAddActionSignalTarget() > 0;
	for ( int i = 0; i < ARRAYSIZE( s_Res ); ++i )
	{
		VPANEL win = FindNamed( TopPanel(), s_Res[i][0], 0 );
		KeyValues *res = win ? LoadFresh( s_Res[i][1] ) : NULL;
		// the editor's controls whose block was taken out
		for ( int c = win ? g_pVPanel->GetChildCount( win ) - 1 : -1; c >= 0 && res; --c )
		{
			VPANEL ch = g_pVPanel->GetChild( win, c );
			if ( IsEditorBlock( g_pVPanel->GetName( ch ) ) && !res->FindKey( g_pVPanel->GetName( ch ) ) && g_pVGui )
				g_pVGui->MarkPanelForDeletion( ch );
		}
		for ( KeyValues *b = res ? res->GetFirstTrueSubKey() : NULL; b; b = b->GetNextTrueSubKey() )
		{
			VPANEL p = FindNamed( win, b->GetName(), 0 );
			void *panel = p ? g_pVPanel->GetPanel( p, "ClientDLL" ) : NULL;
			bool mine = IsEditorBlock( b->GetName() );
			if ( !panel && mine && canMake && *b->GetString( "ControlName" ) && ( g_MakeIn = g_pVPanel->GetPanel( win, "ClientDLL" ) ) != NULL )
			{
				g_MakeInV = win;
				g_MakeClass = b->GetString( "ControlName" );
				g_Made = NULL;
				if ( !SR_SafeCall( MakeOne ) )
					canMake = false, Warning( "[schemereload] making %s/%s crashed; new controls show after a restart\n", s_Res[i][0], b->GetName() );
				panel = g_Made;
			}
			if ( !panel )
				continue;
			char id[256];
			Q_snprintf( id, sizeof( id ), "%s/%s", s_Res[i][0], b->GetName() );
			bool broken = false;
			for ( int j = 0; j < g_ApplyBroken.Count() && !broken; ++j )
				broken = !Q_stricmp( g_ApplyBroken[j], id );
			if ( broken )
				continue;
			for ( const char *k : { "labelText", "text" } ) // the game's texts stay as it set them; the editor's own are the file's
				if ( KeyValues *t = mine ? NULL : b->FindKey( k ) )
					b->RemoveSubKey( t ), t->deleteThis();
			g_ApplyPanel = panel;
			g_ApplyKeys = b;
			if ( !SR_SafeCall( ApplyOne ) )
			{
				g_ApplyBroken.CopyAndAddToTail( id );
				Warning( "[schemereload] applying %s's settings crashed; it's left alone until a restart\n", id );
			}
		}
		if ( res )
			res->deleteThis();
	}
}

static void StepPanels()
{
	std::unordered_map< VPANEL, bool > was;
	RecordVisibility( TopPanel(), was, 0 );

	static bool s_bReapplyBroken;
	if ( !s_bReapplyBroken && !SR_SafeCall( ReapplyLayouts ) )
	{
		s_bReapplyBroken = true;
		Warning( "[schemereload] re-reading the team/class/MOTD layouts crashed; switched off, they update after a restart\n" );
	}

	SR_SafeCall( KeepMenu );
	RefreshPanels( TopPanel(), -2, 0 );
	SR_SafeCall( RestoreMenu );
	// hud_reloadscheme closes an open scoreboard; run it now so the visibility put back below covers that too
	RunStep( STEP_HUD, StepHud );

	g_Reshow.RemoveAll();
	RestoreVisibility( TopPanel(), was, 0 );
	g_flReshowUntil = Plat_FloatTime() + 0.7;
	HideRowTemplates();
}

// Some panels hide themselves a frame after a reload (the scoreboard does whenever its scheme is applied), after the
// visibility above was put back. Shortly after, show again what was visible before; only panels still in the tree.
static void ReshowTree( VPANEL p, int depth )
{
	for ( int i = 0; p && depth < 64 && i < g_pVPanel->GetChildCount( p ); ++i )
	{
		VPANEL c = g_pVPanel->GetChild( p, i );
		if ( c && !g_pVPanel->IsVisible( c ) && g_Reshow.Find( c ) != -1 )
			g_pVPanel->SetVisible( c, true );
		ReshowTree( c, depth + 1 );
	}
}

// The scoreboard's row templates (CTPlayerArea, TPlayerName0...) only give the rows their places; the game hides them
// once at startup and a reload's visibility restore can show them. Keep them hidden.
static bool IsRowTemplate( const char *n )
{
	const char *s = !Q_strnicmp( n, "CTPlayer", 8 ) ? n + 8 : !Q_strnicmp( n, "TPlayer", 7 ) ? n + 7 : NULL;
	static const char *s_Kinds[] = { "Area", "Avatar", "Clan", "Name", "Status", "Score", "Deaths", "Latency" };
	for ( int k = 0; s && k < ARRAYSIZE( s_Kinds ); ++k )
	{
		int len = Q_strlen( s_Kinds[k] );
		if ( Q_strnicmp( s, s_Kinds[k], len ) )
			continue;
		for ( s += len; *s >= '0' && *s <= '9'; ++s )
			;
		return !*s;
	}
	return false;
}

static void HideRowTemplates()
{
	VPANEL sb = FindNamed( TopPanel(), "scores", 0 );
	for ( int i = 0; sb && i < g_pVPanel->GetChildCount( sb ); ++i )
	{
		VPANEL c = g_pVPanel->GetChild( sb, i );
		if ( c && g_pVPanel->IsVisible( c ) && IsRowTemplate( g_pVPanel->GetName( c ) ) )
			g_pVPanel->SetVisible( c, false );
	}
}

static void Reshow()
{
	if ( g_Reshow.Count() && Plat_FloatTime() < g_flReshowUntil )
	{
		ReshowTree( TopPanel(), 0 );
		HideRowTemplates();
	}
	else if ( g_Reshow.Count() )
	{
		g_Reshow.RemoveAll();
		HideRowTemplates();
	}
}

static void StepHud()
{
	bool inGame = g_pEngineClient ? g_pEngineClient->IsInGame() : g_bLevelActive;
	if ( inGame && g_pEngineClient )
		g_pEngineClient->ExecuteClientCmd( "hud_reloadscheme" ); // right away, not queued
	else if ( inGame && g_pEngineServer )
		g_pEngineServer->ServerCommand( "hud_reloadscheme\n" );
}

static bool RunStep( int step, void ( *fn )() )
{
	if ( g_bStepBroken[step] )
		return false;
	if ( SR_SafeCall( fn ) )
		return true;
	g_bStepBroken[step] = true;
	Warning( "[schemereload] the '%s' step crashed and is now switched off until restart (the other steps still run)\n", s_StepNames[step] );
	return false;
}

static void DoReload()
{
	if ( g_bDisabled )
		return;
	g_nColors = g_nFonts = g_nBorders = g_nRefreshed = g_nRefreshFailed = 0;

	if ( RunStep( STEP_READ, StepRead ) )
	{
		RunStep( STEP_COLORS, StepColors );
		RunStep( STEP_FONTS, StepFonts );
		RunStep( STEP_BORDERS, StepBorders );
		if ( !RunStep( STEP_PANELS, StepPanels ) ) // runs StepHud too, unless it's broken
			RunStep( STEP_HUD, StepHud );
	}
	for ( int i = 0; i < NUM_FILES; ++i )
	{
		if ( g_pFresh[i] )
			g_pFresh[i]->deleteThis();
		g_pFresh[i] = NULL;
		g_nLive[i] = 0;
	}

	Msg( "[schemereload] reloaded: %d colour, %d font, %d border value(s) changed; refreshed %d panel group(s)\n",
		g_nColors, g_nFonts, g_nBorders, g_nRefreshed );
	if ( g_nRefreshFailed )
		Warning( "[schemereload] %d panel(s) crashed while refreshing and were skipped\n", g_nRefreshFailed );
}

//-----------------------------------------------------------------------------
// Watching
//-----------------------------------------------------------------------------
static int g_nPendingTicks = -1;
static bool g_bPrimed;

static void Tick()
{
	EnsureMapped();
	if ( !scheme_reload_watch.GetBool() )
		return;

	bool changed = false;
	for ( int i = 0; i < NUM_FILES; ++i )
	{
		SchemeFile &f = g_Files[i];
		FullPathOf( f.relPath, f.fullPath, sizeof( f.fullPath ) );
		unsigned long long t = f.fullPath[0] ? SR_NewestResTime( f.fullPath ) : 0;
		if ( t != f.lastTime )
		{
			changed |= g_bPrimed;
			f.lastTime = t;
		}
	}

	// HUD layout/animation edits only need hud_reloadscheme, which the reload's last step runs
	static const char *s_HudFiles[] = { "scripts/HudLayout.res", "scripts/hudanimations.txt" };
	static unsigned long long s_HudTimes[ARRAYSIZE( s_HudFiles )];
	for ( int i = 0; i < ARRAYSIZE( s_HudFiles ); ++i )
	{
		char full[1024];
		unsigned long long t = FullPathOf( s_HudFiles[i], full, sizeof( full ) ) ? SR_FileTime( full ) : 0;
		if ( t != s_HudTimes[i] )
		{
			changed |= g_bPrimed;
			s_HudTimes[i] = t;
		}
	}
	// window layouts (resource/UI/*.res next to ClientScheme): the panel refresh re-applies them
	static unsigned long long s_uiTime;
	char ui[1100];
	Q_strncpy( ui, g_Files[1].fullPath, sizeof( ui ) );
	char *slash = V_strrchr( ui, '\\' ) > V_strrchr( ui, '/' ) ? V_strrchr( ui, '\\' ) : V_strrchr( ui, '/' );
	if ( slash )
	{
		Q_strncpy( slash + 1, "UI\\x.res", sizeof( ui ) - ( slash + 1 - ui ) );
		unsigned long long t = SR_NewestResTime( ui );
		if ( t != s_uiTime )
		{
			changed |= g_bPrimed;
			s_uiTime = t;
		}
	}
	g_bPrimed = true;

	// wait one extra tick after the last change so we don't read a half-written file
	if ( changed )
		g_nPendingTicks = 1;
	else if ( g_nPendingTicks > 0 )
		--g_nPendingTicks;
	else if ( g_nPendingTicks == 0 )
	{
		// not while a map loads: its windows are being made and torn down, and refreshing them then crashed (a change
		// written just as a map started). It runs once the loading screen has gone.
		if ( g_pEngineClient && g_pEngineClient->IsDrawingLoadingImage() )
			return;
		g_nPendingTicks = -1;
		DoReload();
	}
}

//-----------------------------------------------------------------------------
// Visible panels, for the editor: addons/schemereload_panels.txt lists every visible panel
// a few levels deep ("screen W H", then "depth module name x y w h class scheme keys" per line). The HUD hides
// elements that aren't being drawn, so this is what is actually on screen.
//-----------------------------------------------------------------------------
static bool g_bDumpBroken;

// Which of our files a panel's scheme came from ("-" if neither): the module alone can't tell, e.g. the centre print
// is a client panel that uses SourceScheme.
static const char *SchemeLabel( VPANEL p )
{
	HScheme h = g_pVPanel->GetScheme( p );
	for ( int i = 0; i < NUM_FILES; ++i )
		for ( int c = 0; c < g_Files[i].nHandles; ++c )
			if ( g_Files[i].handles[c] == h )
				return g_Files[i].label;
	return "-";
}

static void DumpTree( VPANEL p, int module, int depth, CUtlBuffer &out )
{
	for ( int i = 0; i < g_pVPanel->GetChildCount( p ); ++i )
	{
		VPANEL c = g_pVPanel->GetChild( p, i );
		if ( !c || !g_pVPanel->IsVisible( c ) )
			continue;
		int m = PanelModule( c ); // every root sits under BaseUI's staticPanel, so check each panel, not just the top ones
		if ( m < 0 )
			m = module;
		int x, y, w, h;
		g_pVPanel->GetAbsPos( c, x, y );
		g_pVPanel->GetSize( c, w, h );
		// last column: "k" while it takes keyboard input (the chat while typing)
		out.Printf( "%d\t%s\t%s\t%d\t%d\t%d\t%d\t%s\t%s\t%s\n", depth, m >= 0 ? s_Modules[m] : "-", g_pVPanel->GetName( c ), x, y, w, h,
			g_pVPanel->GetClassName( c ), SchemeLabel( c ), g_pVPanel->IsKeyBoardInputEnabled( c ) ? "k" : "" );
		if ( depth < 10 )
			DumpTree( c, m, depth + 1, out );
	}
}

static void DumpPanels()
{
	static CUtlBuffer s_last( 0, 0, CUtlBuffer::TEXT_BUFFER );
	CUtlBuffer out( 0, 0, CUtlBuffer::TEXT_BUFFER );
	VPANEL top = TopPanel();
	int w, h;
	g_pVPanel->GetSize( top, w, h );
	out.Printf( "screen\t%d\t%d\n", w, h );
	DumpTree( top, -1, 1, out );
	if ( out.TellPut() == s_last.TellPut() && !memcmp( out.Base(), s_last.Base(), out.TellPut() ) )
		return;
	char path[1024];
	g_pEngineServer->GetGameDir( path, sizeof( path ) );
	Q_strncat( path, "/addons/schemereload_panels.txt", sizeof( path ) );
	if ( SR_WriteFileAtomic( path, (const char *)out.Base(), out.TellPut() ) )
		s_last.CopyBuffer( out );
}

// The editor sends console commands by writing addons/schemereload_cmd.txt; run it once and delete it. They run as if
// typed into the console: the server's command buffer doesn't know client commands (+showscores, messagemode...).
static void RunCommandFile()
{
	char path[1024];
	g_pEngineServer->GetGameDir( path, sizeof( path ) );
	Q_strncat( path, "/addons/schemereload_cmd.txt", sizeof( path ) );
	FILE *f = fopen( path, "rb" );
	if ( !f )
		return;
	static char cmd[8192];
	size_t n = fread( cmd, 1, sizeof( cmd ) - 2, f );
	fclose( f );
	remove( path );
	cmd[n] = '\n';
	cmd[n + 1] = 0;
	if ( g_pEngineClient )
		g_pEngineClient->ClientCmd_Unrestricted( cmd );
	else
		g_pEngineServer->ServerCommand( cmd );
}

//-----------------------------------------------------------------------------
// Test texts: while a map is running, keep sample versions of the texts bhop/surf servers draw on screen.
// schemereload_testtext adds up: 1 timer box (HintText, font HudHintText) and right-side text (KeyHintText,
// HudHintTextSmall), 2 speed (PrintCenterText, Trebuchet24 in SourceScheme), 4 jhud and 8 top-left records
// (both ShowHudText, CenterPrintText in ClientScheme), 16 chat lines (SayText, ChatScheme). A text switched off is
// cleared from the screen (chat lines fade by themselves).
//-----------------------------------------------------------------------------
enum { TEXT_TIMER = 1, TEXT_SPEED = 2, TEXT_JHUD = 4, TEXT_TOPLEFT = 8, TEXT_CHAT = 16 };
static ConVar schemereload_testtext( "schemereload_testtext", "0", 0,
	"Sample server texts to keep on screen, added up: 1 timer box and right side, 2 speed (centre print), 4 jhud, 8 top left, 16 chat" );
static IServerGameDLL *g_pServerGame;
static int g_msgTextMsg = -1, g_msgHintText = -1, g_msgKeyHintText = -1, g_msgHudMsg = -1, g_msgSayText = -1;

class CHostFilter : public IRecipientFilter
{
public:
	virtual bool IsReliable() const { return false; }
	virtual bool IsInitMessage() const { return false; }
	virtual int GetRecipientCount() const { return 1; }
	virtual int GetRecipientIndex( int ) const { return 1; } // the listen server's own player
};

static void FindUserMessages()
{
	char name[64];
	int size;
	for ( int i = 0; g_pServerGame && g_pServerGame->GetUserMessageInfo( i, name, sizeof( name ), size ); ++i )
	{
		if ( !Q_stricmp( name, "TextMsg" ) ) g_msgTextMsg = i;
		else if ( !Q_stricmp( name, "HintText" ) ) g_msgHintText = i;
		else if ( !Q_stricmp( name, "KeyHintText" ) ) g_msgKeyHintText = i;
		else if ( !Q_stricmp( name, "HudMsg" ) ) g_msgHudMsg = i;
		else if ( !Q_stricmp( name, "SayText" ) ) g_msgSayText = i;
	}
}

static void SendCentre( CHostFilter &filter, const char *text )
{
	bf_write *msg = g_pEngineServer->UserMessageBegin( &filter, g_msgTextMsg );
	msg->WriteByte( 4 ); // HUD_PRINTCENTER
	msg->WriteString( text );
	for ( int i = 0; i < 4; ++i )
		msg->WriteString( "" );
	g_pEngineServer->MessageEnd();
}

// Empty strings hide the boxes. The hint's sound (ui/hint.wav) is silenced by addons/schemereload_sounds, see Load.
static void SendHints( CHostFilter &filter, const char *hint, const char *key )
{
	bf_write *msg = g_pEngineServer->UserMessageBegin( &filter, g_msgHintText );
	msg->WriteString( hint );
	g_pEngineServer->MessageEnd();
	msg = g_pEngineServer->UserMessageBegin( &filter, g_msgKeyHintText );
	msg->WriteByte( 1 );
	msg->WriteString( key );
	g_pEngineServer->MessageEnd();
}

// ShowHudText: x/y are screen fractions (-1 = centred). Held 0.5 s, so it goes away by itself once no longer sent.
static void SendHudText( CHostFilter &filter, int channel, float x, float y, const char *text )
{
	bf_write *msg = g_pEngineServer->UserMessageBegin( &filter, g_msgHudMsg );
	msg->WriteByte( channel );
	msg->WriteFloat( x );
	msg->WriteFloat( y );
	for ( int i = 0; i < 8; ++i )
		msg->WriteByte( 255 );	// colour 1 and 2: white
	msg->WriteByte( 0 );		// effect
	msg->WriteFloat( 0.0f );	// fade in
	msg->WriteFloat( 0.0f );	// fade out
	msg->WriteFloat( 0.5f );	// hold (re-sent every 0.1 s)
	msg->WriteFloat( 0.0f );	// fx time
	msg->WriteString( text );
	g_pEngineServer->MessageEnd();
}

// A chat line as the listen server's own player says it: \x03 = the player's team colour, \x01 = normal chat text.
static void SendChat( CHostFilter &filter, const char *text )
{
	bf_write *msg = g_pEngineServer->UserMessageBegin( &filter, g_msgSayText );
	msg->WriteByte( 1 );
	msg->WriteString( text );
	msg->WriteByte( 1 ); // chat, not a console-only line
	g_pEngineServer->MessageEnd();
}

static void SendTestTexts()
{
	static int s_shown;
	int show = schemereload_testtext.GetInt();
	if ( !show && !s_shown )
		return;
	if ( g_msgTextMsg < 0 )
		FindUserMessages();
	if ( g_msgTextMsg < 0 || g_msgHintText < 0 || g_msgKeyHintText < 0 || g_msgHudMsg < 0 )
		return;
	CHostFilter filter;
	int off = s_shown & ~show;
	s_shown = show;
	if ( off & TEXT_TIMER )
		SendHints( filter, "", "" );
	if ( off & TEXT_SPEED )
		SendCentre( filter, "" );

	double t = Plat_FloatTime();
	int speed = 250 + (int)( 60 * sin( t ) ), jumps = (int)t % 40;
	char text[256];
	if ( show & TEXT_TIMER )
	{
		Q_snprintf( text, sizeof( text ), "[Normal]\nTime: %02d:%06.3f\nSpeed: %d u/s\nJumps: %d", (int)( t / 60 ) % 60, fmod( t, 60.0 ), speed, jumps );
		SendHints( filter, text, "Spectators (2):\nsomeone\nsomeone else\n\nMap: surf_test\nTier 3" );
	}
	if ( show & TEXT_SPEED )
	{
		Q_snprintf( text, sizeof( text ), "Speed: %d", speed );
		SendCentre( filter, text );
	}
	if ( show & TEXT_JHUD ) // "jump: speed", just under the centre print (which is at 0.35)
	{
		Q_snprintf( text, sizeof( text ), "%d: %d", jumps, speed );
		SendHudText( filter, 1, -1.0f, 0.38f, text );
	}
	if ( show & TEXT_TOPLEFT ) // record times, where timers put them
		SendHudText( filter, 2, 0.01f, 0.01f, "WR: 17.707 (someone)\n\nPB: 17.966 (you) (Normal)" );
	static double s_nextChat;
	if ( ( show & TEXT_CHAT ) && g_msgSayText >= 0 && t >= s_nextChat ) // a new line every 3 s keeps a few on screen
	{
		static const char *s_lines[] = { "\x03player\x01 :  gg that was a clean run", "\x03player\x01 :  what tier is this map?", "\x04[Timer]\x01 New personal best: 17.966" };
		static int s_line;
		s_nextChat = t + 3.0;
		SendChat( filter, s_lines[s_line++ % ARRAYSIZE( s_lines )] );
	}
}

static void TimerTick()
{
	if ( g_bDisabled )
		return;
	SR_SafeCall( RunCommandFile );
	if ( !g_bDumpBroken && g_pEngineServer && !SR_SafeCall( DumpPanels ) )
	{
		g_bDumpBroken = true;
		Warning( "[schemereload] crashed while listing panels for the editor - switched off until restart\n" );
	}
	static bool s_announced;
	if ( !s_announced )
	{
		s_announced = true;
		Msg( "[schemereload] watching for scheme changes\n" );
	}
	if ( !SR_SafeCall( Tick ) )
	{
		g_bDisabled = true;
		Warning( "[schemereload] crashed while watching - plugin disabled until the game restarts\n" );
	}
}

//-----------------------------------------------------------------------------
// Test values (the editor's Test tools): the listen server's own player's health, armor, money or magazine, set
// through the networked fields' offsets (found by name in the server classes' send tables), and a round time
// beyond mp_roundtime's 1-9 minutes.
//-----------------------------------------------------------------------------
void ConVarUnbound( ConVar *cv );
static int PropOffset( SendTable *t, const char *name, int depth = 0 )
{
	for ( int i = 0; t && depth < 8 && i < t->GetNumProps(); ++i )
	{
		SendProp *p = t->GetProp( i );
		if ( !Q_stricmp( p->GetName(), name ) && p->GetType() != DPT_DataTable )
			return p->GetOffset();
		if ( p->GetType() == DPT_DataTable && p->GetDataTable() )
		{
			int o = PropOffset( p->GetDataTable(), name, depth + 1 );
			if ( o >= 0 )
				return p->GetOffset() + o;
		}
	}
	return -1;
}
// The field `prop` of entity `index` (or -1), with its edict for marking the change.
static int *EntityInt( int index, const char *prop, edict_t **out )
{
	edict_t *e = g_pEngineServer ? g_pEngineServer->PEntityOfEntIndex( index ) : NULL;
	IServerNetworkable *n = e && !e->IsFree() ? e->GetNetworkable() : NULL;
	ServerClass *sc = n ? n->GetServerClass() : NULL;
	int off = sc ? PropOffset( sc->m_pTable, prop ) : -1;
	void *ent = off >= 0 && e->GetUnknown() ? e->GetUnknown()->GetBaseEntity() : NULL;
	*out = e;
	return ent ? (int *)( (char *)ent + off ) : NULL;
}
CON_COMMAND( schemereload_testvalue, "schemereload_testvalue health|armor|money|clip <n> | roundtime <minutes>: sets it for the listen server's own player" )
{
	if ( args.ArgC() < 3 )
		return;
	const char *what = args.Arg( 1 );
	if ( !Q_stricmp( what, "roundtime" ) )
	{
		if ( ConVar *rt = g_pCVar->FindVar( "mp_roundtime" ) )
		{
			ConVarUnbound( rt );
			rt->SetValue( args.Arg( 2 ) );
			g_pEngineServer->ServerCommand( "mp_restartgame 1\n" );
		}
		return;
	}
	static const char *s_Props[][2] = { { "health", "m_iHealth" }, { "armor", "m_ArmorValue" }, { "money", "m_iAccount" }, { "clip", "m_iClip1" } };
	for ( auto &p : s_Props )
	{
		if ( Q_stricmp( what, p[0] ) )
			continue;
		int index = 1;
		edict_t *e;
		if ( !Q_stricmp( what, "clip" ) ) // the player's weapon in hand
		{
			int *h = EntityInt( 1, "m_hActiveWeapon", &e );
			index = h ? *h & ( ( 1 << MAX_EDICT_BITS ) - 1 ) : 0;
		}
		int *v = index ? EntityInt( index, p[1], &e ) : NULL;
		if ( !v )
		{
			Msg( "[schemereload] no %s to set\n", what );
			return;
		}
		*v = atoi( args.Arg( 2 ) );
		e->m_fStateFlags |= FL_EDICT_CHANGED | FL_FULL_EDICT_CHANGED; // StateChanged() without the engine's change list
		return;
	}
}

// Panels the game shows only now and then, on the test server: the round-end panel (as for a CT win, with the player
// as MVP and a fun fact), the killer panel (as if a bot had killed the player), or neither (hide).
static IGameEventManager2 *g_pGameEvents;
CON_COMMAND( schemereload_showpanel, "schemereload_showpanel win|freeze|hide: shows the round-end or killer panel on the test server" )
{
	edict_t *me = g_pEngineServer ? g_pEngineServer->PEntityOfEntIndex( 1 ) : NULL;
	if ( !g_pGameEvents || !me || args.ArgC() < 2 )
		return;
	int bot = 0;
	for ( int i = 2; i <= 64 && !bot; ++i )
	{
		edict_t *e = g_pEngineServer->PEntityOfEntIndex( i );
		if ( e && !e->IsFree() && g_pEngineServer->GetPlayerUserId( e ) >= 0 && !Q_stricmp( g_pEngineServer->GetPlayerNetworkIDString( e ), "BOT" ) )
			bot = i;
	}
	const char *what = args.Arg( 1 );
	auto fire = [&]( const char *name, void ( *fill )( IGameEvent *, int, int ) ) {
		if ( IGameEvent *e = g_pGameEvents->CreateEvent( name, true ) )
		{
			if ( fill )
				fill( e, g_pEngineServer->GetPlayerUserId( me ), bot );
			g_pGameEvents->FireEvent( e );
		}
	};
	if ( !Q_stricmp( what, "win" ) )
	{
		fire( "cs_win_panel_round", []( IGameEvent *e, int, int ) {
			e->SetBool( "show_timer_defend", false );
			e->SetBool( "show_timer_attack", true );
			e->SetInt( "timer_time", 83 );
			e->SetInt( "final_event", 7 ); // CTs_Win in cs_gamerules.h
			e->SetString( "funfact_token", "#funfact_killed_enemies" );
			e->SetInt( "funfact_player", 1 );
			e->SetInt( "funfact_data1", 5 );
		} );
		fire( "round_mvp", []( IGameEvent *e, int me, int ) { e->SetInt( "userid", me ); e->SetInt( "reason", 1 ); } );
	}
	else if ( !Q_stricmp( what, "freeze" ) )
		fire( "show_freezepanel", []( IGameEvent *e, int, int bot ) { e->SetInt( "killer", bot ? bot : 1 ); } );
	else
	{
		fire( "hide_freezepanel", NULL );
		fire( "round_start", []( IGameEvent *e, int, int ) { e->SetInt( "timelimit", 540 ); } ); // the round-end panel goes on this
	}
}

//-----------------------------------------------------------------------------
// Console commands
//-----------------------------------------------------------------------------
CON_COMMAND( scheme_font_info, "scheme_font_info <font>: which windows font and size the game really uses for it" )
{
	if ( args.ArgC() < 2 || !g_pSurface )
		return;
	for ( int i = 0; i < NUM_FILES; ++i )
	{
		for ( int c = 0; c < g_Files[i].nHandles; ++c )
		{
			IScheme *s = g_pSchemeMgr->GetIScheme( g_Files[i].handles[c] );
			for ( int prop = 0; s && prop < 2; ++prop )
			{
				HFont h = s->GetFont( args[1], prop != 0 );
				if ( h )
					Msg( "[schemereload] %s copy %d %s: font %d = '%s' family '%s' tall %d\n", g_Files[i].label, c, prop ? "proportional" : "fixed",
						(int)h, g_pSurface->GetFontName( h ), g_pSurface->GetFontFamilyName( h ), g_pSurface->GetFontTall( h ) );
			}
		}
	}
}

CON_COMMAND( scheme_reload, "Re-read SourceScheme.res and ClientScheme.res and apply them now" )
{
	DoReload();
}

//-----------------------------------------------------------------------------
// Search paths. The editor edits a HUD in place: the game searches custom folders alphabetically (the first one
// with a file wins) and only knows the folders that were there when it started, so the HUD being edited goes first.
// The watcher then finds the scheme files somewhere new and reloads.
//-----------------------------------------------------------------------------
static void MountFirst( const char *dir )
{
	g_pFS->RemoveSearchPath( dir, "GAME" );
	g_pFS->RemoveSearchPath( dir, "MOD" );
	g_pFS->AddSearchPath( dir, "GAME", PATH_ADD_TO_HEAD );
	g_pFS->AddSearchPath( dir, "MOD", PATH_ADD_TO_HEAD );
}

CON_COMMAND( schemereload_mount, "schemereload_mount <folder>: search this folder before all others for game files (the HUD being edited)" )
{
	static char s_mounted[MAX_PATH];
	static bool s_added; // wasn't a search path before, so it goes again when another folder takes its place
	if ( args.ArgC() < 2 )
		return;
	char dir[MAX_PATH];
	V_strncpy( dir, args[1], sizeof( dir ) );
	V_FixSlashes( dir );
	V_AppendSlash( dir, sizeof( dir ) );
	if ( s_added && V_stricmp( s_mounted, dir ) )
	{
		g_pFS->RemoveSearchPath( s_mounted, "GAME" );
		g_pFS->RemoveSearchPath( s_mounted, "MOD" );
	}
	static char s_paths[65536];
	g_pFS->GetSearchPath( "GAME", false, s_paths, sizeof( s_paths ) );
	s_added = !V_stristr( s_paths, dir ) || ( s_added && !V_stricmp( s_mounted, dir ) );
	MountFirst( dir );
	V_strncpy( s_mounted, dir, sizeof( s_mounted ) );
	Msg( "[schemereload] searching %s first\n", dir );
}

static void PrintStatus()
{
	for ( int i = 0; i < NUM_FILES; ++i )
	{
		SchemeFile &f = g_Files[i];
		char full[1024];
		if ( !FullPathOf( f.relPath, full, sizeof( full ) ) )
			Q_strncpy( full, "(file not found)", sizeof( full ) );
		Msg( "[schemereload] %s: %s\n", f.label, full );

		KeyValues *fresh = LoadFresh( f.relPath );
		if ( !fresh )
		{
			Msg( "    couldn't parse the file\n" );
			continue;
		}
		KeyValues *colors = fresh->FindKey( "Colors" );
		KeyValues *base = fresh->FindKey( "BaseSettings" );
		int nColors = 0, nBase = 0;
		for ( KeyValues *k = colors ? colors->GetFirstValue() : NULL; k; k = k->GetNextValue() )
			++nColors;
		for ( KeyValues *k = base ? base->GetFirstValue() : NULL; k; k = k->GetNextValue() )
			++nBase;
		Msg( "    file has %d Colors and %d BaseSettings entries%s\n", nColors, nBase,
			fresh->FindKey( "Fonts" ) ? "" : " (no Fonts section - did its #base load?)" );

		CUtlVector< HScheme > candidates;
		GatherCandidates( f, candidates );
		for ( int c = 0; c < candidates.Count(); ++c )
		{
			Msg( "    scheme %d: match score %d%s\n", (int)candidates[c], ScoreScheme( candidates[c], fresh ),
				f.Owns( candidates[c] ) ? "  <- in use" : "" );
		}
		fresh->deleteThis();
	}
	Msg( "[schemereload] watch=%d fonts=%d%s\n", scheme_reload_watch.GetInt(), scheme_reload_fonts.GetInt(),
		g_bDisabled ? " (DISABLED after a crash)" : "" );
	for ( int s = 0; s < STEP_COUNT; ++s )
	{
		if ( g_bStepBroken[s] )
			Msg( "[schemereload] step '%s' is switched off after a crash\n", s_StepNames[s] );
	}
}

CON_COMMAND( scheme_reload_status, "Show what schemereload is watching and how it matched the schemes in memory" )
{
	if ( !SR_SafeCall( PrintStatus ) )
		Warning( "[schemereload] crashed while printing status\n" );
}

//-----------------------------------------------------------------------------
// Plugin
//-----------------------------------------------------------------------------
class CSchemeReloadPlugin : public IServerPluginCallbacks
{
public:
	virtual bool Load( CreateInterfaceFn interfaceFactory, CreateInterfaceFn gameServerFactory );
	virtual void Unload();
	virtual void Pause() {}
	virtual void UnPause() {}
	virtual const char *GetPluginDescription() { return "schemereload - live SourceScheme/ClientScheme reloading"; }
	virtual void LevelInit( char const *pMapName ) { g_bLevelActive = true; }
	virtual void ServerActivate( edict_t *pEdictList, int edictCount, int clientMax ) {}
	virtual void GameFrame( bool simulating );
	virtual void LevelShutdown() { g_bLevelActive = false; }
	virtual void ClientActive( edict_t *pEntity ) {}
	virtual void ClientDisconnect( edict_t *pEntity ) {}
	virtual void ClientPutInServer( edict_t *pEntity, char const *playername ) {}
	virtual void SetCommandClient( int index ) {}
	virtual void ClientSettingsChanged( edict_t *pEdict ) {}
	virtual PLUGIN_RESULT ClientConnect( bool *bAllowConnect, edict_t *pEntity, const char *pszName, const char *pszAddress, char *reject, int maxrejectlen ) { return PLUGIN_CONTINUE; }
	virtual PLUGIN_RESULT ClientCommand( edict_t *pEntity, const CCommand &args ) { return PLUGIN_CONTINUE; }
	virtual PLUGIN_RESULT NetworkIDValidated( const char *pszUserName, const char *pszNetworkID ) { return PLUGIN_CONTINUE; }
	virtual void OnQueryCvarValueFinished( QueryCvarCookie_t iCookie, edict_t *pPlayerEntity, EQueryCvarValueStatus eStatus, const char *pCvarName, const char *pCvarValue ) {}
	virtual void OnEdictAllocated( edict_t *edict ) {}
	virtual void OnEdictFreed( const edict_t *edict ) {}
};

static CSchemeReloadPlugin g_Plugin;
EXPOSE_SINGLE_INTERFACE_GLOBALVAR( CSchemeReloadPlugin, IServerPluginCallbacks, INTERFACEVERSION_ISERVERPLUGINCALLBACKS, g_Plugin );

bool CSchemeReloadPlugin::Load( CreateInterfaceFn interfaceFactory, CreateInterfaceFn gameServerFactory )
{
	ConnectTier1Libraries( &interfaceFactory, 1 );

	g_pFS = (IFileSystem *)interfaceFactory( FILESYSTEM_INTERFACE_VERSION, NULL );
	g_pEngineServer = (IVEngineServer *)interfaceFactory( INTERFACEVERSION_VENGINESERVER, NULL );
	g_pEngineClient = (IVEngineClient *)interfaceFactory( VENGINE_CLIENT_INTERFACE_VERSION, NULL );
	g_pEngineVGui = (IEngineVGui *)interfaceFactory( VENGINE_VGUI_VERSION, NULL );
	g_pServerGame = gameServerFactory ? (IServerGameDLL *)gameServerFactory( INTERFACEVERSION_SERVERGAMEDLL, NULL ) : NULL;
	g_pGameEvents = (IGameEventManager2 *)interfaceFactory( INTERFACEVERSION_GAMEEVENTSMANAGER2, NULL );

	CreateInterfaceFn vguiFactory = Sys_GetFactory( "vgui2.dll" );
	if ( vguiFactory )
	{
		g_pSchemeMgr = (ISchemeManager *)vguiFactory( VGUI_SCHEME_INTERFACE_VERSION, NULL );
		g_pVPanel = (IPanel *)vguiFactory( VGUI_PANEL_INTERFACE_VERSION, NULL );
		g_pVGui = (IVGui *)vguiFactory( VGUI_IVGUI_INTERFACE_VERSION, NULL );
	}
	if ( CreateInterfaceFn surfaceFactory = Sys_GetFactory( "vguimatsurface.dll" ) )
	{
		g_pSurface = (ISurface *)surfaceFactory( VGUI_SURFACE_INTERFACE_VERSION, NULL );
	}

	if ( !g_pFS || !g_pEngineVGui || !g_pSchemeMgr || !g_pVPanel || !g_pCVar )
	{
		Warning( "[schemereload] missing a game interface (fs=%p vgui=%p scheme=%p panel=%p cvar=%p), not loading\n",
			g_pFS, g_pEngineVGui, g_pSchemeMgr, g_pVPanel, g_pCVar );
		return false;
	}

	ConVar_Register( 0 );

	// The editor puts a silent ui/hint.wav in addons/schemereload_sounds: the test texts' hints beep ten times a second.
	char sounds[MAX_PATH];
	g_pEngineServer->GetGameDir( sounds, sizeof( sounds ) );
	V_strncat( sounds, "/addons/schemereload_sounds/", sizeof( sounds ) );
	V_FixSlashes( sounds );
	if ( g_pFS->IsDirectory( sounds ) )
		MountFirst( sounds );

	if ( !SR_StartTimer( TimerTick, 300 ) )
		Warning( "[schemereload] couldn't start the file watcher timer; use scheme_reload manually\n" );

	Msg( "[schemereload] loaded. Save a scheme file or type scheme_reload.\n" );
	return true;
}

void CSchemeReloadPlugin::Unload()
{
	SR_StopTimer();
	if ( g_EditorBorders.Count() && SR_SafeCall( RemoveEditorBorders ) )
		SR_SafeCall( StepPanels ); // panels fetch their borders again
	ConVar_Unregister();
	DisconnectTier1Libraries();
}

void CSchemeReloadPlugin::GameFrame( bool simulating )
{
	// backup trigger while a map is running, in case the timer isn't being dispatched
	static double s_next, s_nextText;
	static bool s_textBroken;
	double now = Plat_FloatTime();
	if ( g_Reshow.Count() && !g_bStepBroken[STEP_PANELS] )
		RunStep( STEP_PANELS, Reshow );
	if ( simulating && !s_textBroken && now >= s_nextText )
	{
		s_nextText = now + 0.1;
		if ( !SR_SafeCall( SendTestTexts ) )
		{
			s_textBroken = true;
			Warning( "[schemereload] crashed while sending test texts - switched off until restart\n" );
		}
	}
	if ( now < s_next )
		return;
	s_next = now + 0.3;
	TimerTick();
}
