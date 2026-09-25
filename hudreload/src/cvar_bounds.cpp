// Takes a console variable's min/max away, as SourceMod's SetConVarBounds does: the test server's round time can then
// be over mp_roundtime's 9 minutes. ConVar keeps them private; the layout is the SDK's (same as the game's).
#define _ALLOW_KEYWORD_MACROS
#define private public
#include "tier1/convar.h"
#undef private

void ConVarUnbound( ConVar *cv )
{
	for ( ConVar *c = cv; c; c = c->m_pParent != c ? c->m_pParent : NULL )
		c->m_bHasMin = c->m_bHasMax = false;
}
