/*
Copyright (C) 1996-1997 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include "cmd.h"
#include "console.h"
#include "qwsvdef.h"
#include "server.h"
#include "sys.h"
#include "zone.h"

#include <streams/file_stream.h>

/* Forward declarations */
int rfclose(RFILE* stream);

qboolean sv_allow_cheats;

int fp_messages = 4, fp_persecond = 4, fp_secondsdead = 10;
char fp_msg[255] = { 0 };

/*
===============================================================================

OPERATOR CONSOLE ONLY COMMANDS

These commands can only be entered from stdin or by a remote operator datagram
===============================================================================
*/

/*
====================
SV_SetMaster_f

Make a master server current
====================
*/
void
SV_SetMaster_f(void)
{
    char data[2];
    int i;

    memset(&master_adr, 0, sizeof(master_adr));

    for (i = 1; i < Cmd_Argc(); i++) {
	if (!strcmp(Cmd_Argv(i), "none")
	    || !NET_StringToAdr(Cmd_Argv(i), &master_adr[i - 1])) {
	    Con_Printf("Setting nomaster mode.\n");
	    return;
	}
	if (master_adr[i - 1].port == 0)
	    master_adr[i - 1].port = BigShort(27000);

	Con_Printf("Master server at %s\n",
		   NET_AdrToString(master_adr[i - 1]));

	Con_Printf("Sending a ping.\n");

	data[0] = A2A_PING;
	data[1] = 0;
	NET_SendPacket(2, data, master_adr[i - 1]);
    }

    svs.last_heartbeat = -99999;
}


/*
==================
SV_Quit_f
==================
*/
void
SV_Quit_f(void)
{
    SV_FinalMessage("server shutdown\n");
    Con_Printf("Shutting down.\n");
    SV_Shutdown();
    Sys_Quit();
}

/*
==================
SV_SetPlayer

Sets host_client and sv_player to the player with idnum Cmd_Argv(1)
==================
*/
qboolean
SV_SetPlayer(void)
{
    client_t *cl;
    int i;
    int idnum;

    idnum = atoi(Cmd_Argv(1));

    for (i = 0, cl = svs.clients; i < MAX_CLIENTS; i++, cl++) {
	if (!cl->state)
	    continue;
	if (cl->userid == idnum) {
	    host_client = cl;
	    sv_player = host_client->edict;
	    return true;
	}
    }
    Con_Printf("Userid %i is not on the server\n", idnum);
    return false;
}


/*
==================
SV_God_f

Sets client to godmode
==================
*/
void
SV_God_f(void)
{
    if (!sv_allow_cheats) {
	Con_Printf
	    ("You must run the server with -cheats to enable this command.\n");
	return;
    }

    if (!SV_SetPlayer())
	return;

    sv_player->v.flags = (int)sv_player->v.flags ^ FL_GODMODE;
    if (!((int)sv_player->v.flags & FL_GODMODE))
	SV_ClientPrintf(host_client, PRINT_HIGH, "godmode OFF\n");
    else
	SV_ClientPrintf(host_client, PRINT_HIGH, "godmode ON\n");
}


void
SV_Noclip_f(void)
{
    if (!sv_allow_cheats) {
	Con_Printf
	    ("You must run the server with -cheats to enable this command.\n");
	return;
    }

    if (!SV_SetPlayer())
	return;

    if (sv_player->v.movetype != MOVETYPE_NOCLIP) {
	sv_player->v.movetype = MOVETYPE_NOCLIP;
	SV_ClientPrintf(host_client, PRINT_HIGH, "noclip ON\n");
    } else {
	sv_player->v.movetype = MOVETYPE_WALK;
	SV_ClientPrintf(host_client, PRINT_HIGH, "noclip OFF\n");
    }
}


/*
==================
SV_Give_f
==================
*/
void
SV_Give_f(void)
{
    const char *t;
    int v;

    if (!sv_allow_cheats) {
	Con_Printf
	    ("You must run the server with -cheats to enable this command.\n");
	return;
    }

    if (!SV_SetPlayer())
	return;

    t = Cmd_Argv(2);
    v = atoi(Cmd_Argv(3));

    switch (t[0]) {
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
	sv_player->v.items =
	    (int)sv_player->v.items | IT_SHOTGUN << (t[0] - '2');
	break;

    case 's':
	sv_player->v.ammo_shells = v;
	break;
    case 'n':
	sv_player->v.ammo_nails = v;
	break;
    case 'r':
	sv_player->v.ammo_rockets = v;
	break;
    case 'h':
	sv_player->v.health = v;
	break;
    case 'c':
	sv_player->v.ammo_cells = v;
	break;
    }
}


/*
======================
SV_Map_f

handle a
map <mapname>
command from the console or progs.
======================
*/
void
SV_Map_f(void)
{
    char level[MAX_QPATH];
    char expanded[MAX_QPATH];
    RFILE *f;

    if (Cmd_Argc() != 2) {
	Con_Printf("map <levelname> : continue game on a new level\n");
	if (sv.name[0])
	    Con_Printf ("Currently on: %s\n",sv.name);
	return;
    }
    strcpy(level, Cmd_Argv(1));

    // check to make sure the level exists
    sprintf(expanded, "maps/%s.bsp", level);
    COM_FOpenFile(expanded, &f);
    if (!f) {
	Con_Printf("Can't find %s\n", expanded);
	return;
    }
    rfclose(f);

    SV_BroadcastCommand("changing\n");
    SV_SendMessagesToAll();

    SV_SpawnServer(level);

    SV_BroadcastCommand("reconnect\n");
}

static struct stree_root *
SV_Map_Arg_f(const char *arg)
{
    struct stree_root *root = Z_Malloc(sizeof(struct stree_root));
    if (root) {
	*root = STREE_ROOT;

	STree_AllocInit();
	COM_ScanDir(root, "maps", arg, ".bsp", true);
    }
    return root;
}

/*
==================
SV_Kick_f

Kick a user off of the server
==================
*/
void
SV_Kick_f(void)
{
    int i;
    client_t *cl;
    int uid = atoi(Cmd_Argv(1));

    for (i = 0, cl = svs.clients; i < MAX_CLIENTS; i++, cl++) {
	if (!cl->state)
	    continue;
	if (cl->userid == uid) {
	    SV_BroadcastPrintf(PRINT_HIGH, "%s was kicked\n", cl->name);
	    // print directly, because the dropped client won't get the
	    // SV_BroadcastPrintf message
	    SV_ClientPrintf(cl, PRINT_HIGH,
			    "You were kicked from the game\n");
	    SV_DropClient(cl);
	    return;
	}
    }

    Con_Printf("Couldn't find user number %i\n", uid);
}


/*
================
SV_Status_f
================
*/
void
SV_Status_f(void)
{
    const char *addr;
    int i;
    client_t *client;
    float avg, pak;
    float cpu = (svs.stats.latched_active + svs.stats.latched_idle);
    if (cpu)
	cpu = 100 * svs.stats.latched_active / cpu;
    avg = 1000 * svs.stats.latched_active / STATFRAMES;
    pak = (float)svs.stats.latched_packets / STATFRAMES;

    Con_Printf("net address      : %s\n", NET_AdrToString(net_local_adr));
    Con_Printf("cpu utilization  : %3i%%\n", (int)cpu);
    Con_Printf("avg response time: %i ms\n", (int)avg);
    Con_Printf("packets/frame    : %5.2f\n", pak);

// min fps lat drp
    if (sv_redirected != RD_NONE) {
	// most remote clients are 40 columns
	//          0123456789012345678901234567890123456789
	Con_Printf("name               userid frags\n"
		   "  address          rate ping drop\n"
		   "  ---------------- ---- ---- -----\n");
	for (i = 0, client = svs.clients; i < MAX_CLIENTS; i++, client++) {
	    if (!client->state)
		continue;

	    Con_Printf("%-16.16s  %6i %5i%s\n", client->name, client->userid,
		       (int)client->edict->v.frags,
		       client->spectator ? " (s)" : "");

	    addr = NET_BaseAdrToString(client->netchan.remote_address);
	    Con_Printf("  %-16.16s ", addr);
	    if (client->state == cs_connected) {
		Con_Printf("CONNECTING\n");
		continue;
	    }
	    if (client->state == cs_zombie) {
		Con_Printf("ZOMBIE\n");
		continue;
	    }
	    Con_Printf("%4i %4i %5.2f\n",
		       (int)(1000 * client->netchan.frame_rate),
		       (int)SV_CalcPing(client),
		       100.0 * client->netchan.drop_count /
		       client->netchan.incoming_sequence);
	}
    } else {
	Con_Printf("frags userid address         name            rate ping drop  qport\n");
	Con_Printf("----- ------ --------------- --------------- ---- ---- ----- -----\n");
	for (i = 0, client = svs.clients; i < MAX_CLIENTS; i++, client++) {
	    if (!client->state)
		continue;
	    addr = NET_BaseAdrToString(client->netchan.remote_address);
	    Con_Printf("%5i %6i %-16.16s %-16.16s ",
		       (int)client->edict->v.frags, client->userid, addr,
		       client->name);

	    if (client->state == cs_connected) {
		Con_Printf("CONNECTING\n");
		continue;
	    }
	    if (client->state == cs_zombie) {
		Con_Printf("ZOMBIE\n");
		continue;
	    }
	    Con_Printf("%4i %4i %3.1f %4i%s\n",
		       (int)(1000 * client->netchan.frame_rate),
		       (int)SV_CalcPing(client),
		       100.0 * client->netchan.drop_count /
		       client->netchan.incoming_sequence,
		       client->netchan.qport,
		       client->spectator ? " (s)" : "");
	}
    }
    Con_Printf("\n");
}

/*
==================
SV_ConSay_f
==================
*/
void
SV_ConSay_f(void)
{
    client_t *client;
    int i;
    size_t len, space;
    const char *p;
    char text[1024];

    if (Cmd_Argc() < 2)
	return;

    strcpy(text, "console: ");

    len = strlen(text);
    space = sizeof(text) - len - 2; // -2 for \n and null terminator
    p = Cmd_Args();
    if (*p == '"') {
	/* remove quotes */
	strncat(text, p + 1, qmin(strlen(p) - 2, space));
	text[len + qmin(strlen(p) - 2, space)] = 0;
    } else {
	strncat(text, p, space);
	text[len + qmin(strlen(p), space)] = 0;
    }
    strcat(text, "\n");

    for (i = 0, client = svs.clients; i < MAX_CLIENTS; i++, client++) {
	if (client->state != cs_spawned)
	    continue;
	SV_ClientPrintf(client, PRINT_CHAT, "%s\n", text);
    }
}


/*
==================
SV_Heartbeat_f
==================
*/
void
SV_Heartbeat_f(void)
{
    svs.last_heartbeat = -9999;
}

void
SV_SendServerInfoChange(const char *key, const char *value)
{
    if (!sv.state)
	return;

    MSG_WriteByte(&sv.reliable_datagram, svc_serverinfo);
    MSG_WriteString(&sv.reliable_datagram, key);
    MSG_WriteString(&sv.reliable_datagram, value);
}

/*
===========
SV_Serverinfo_f

  Examine or change the serverinfo string
===========
*/
void
SV_Serverinfo_f(void)
{
    if (Cmd_Argc() == 1) {
	Con_Printf("Server info settings:\n");
	Info_Print(svs.info);
	return;
    }

    if (Cmd_Argc() != 3) {
	Con_Printf("usage: serverinfo [ <key> <value> ]\n");
	return;
    }

    if (Cmd_Argv(1)[0] == '*') {
	Con_Printf("Star variables cannot be changed.\n");
	return;
    }
    Info_SetValueForKey(svs.info, Cmd_Argv(1), Cmd_Argv(2),
			MAX_SERVERINFO_STRING);

    // if this is a cvar, change it too
    // FIXME - double cvar search just to avoid a Con_Printf
    if (Cvar_FindVar(Cmd_Argv(1)))
	Cvar_Set(Cmd_Argv(1), Cmd_Argv(2));

    SV_SendServerInfoChange(Cmd_Argv(1), Cmd_Argv(2));
}


/*
===========
SV_Serverinfo_f

  Examine or change the serverinfo string
===========
*/
void
SV_Localinfo_f(void)
{
    if (Cmd_Argc() == 1) {
	Con_Printf("Local info settings:\n");
	Info_Print(localinfo);
	return;
    }

    if (Cmd_Argc() != 3) {
	Con_Printf("usage: localinfo [ <key> <value> ]\n");
	return;
    }

    if (Cmd_Argv(1)[0] == '*') {
	Con_Printf("Star variables cannot be changed.\n");
	return;
    }
    Info_SetValueForKey(localinfo, Cmd_Argv(1), Cmd_Argv(2),
			MAX_LOCALINFO_STRING);
}


/*
===========
SV_User_f

Examine a users info strings
===========
*/
void
SV_User_f(void)
{
    if (Cmd_Argc() != 2) {
	Con_Printf("Usage: info <userid>\n");
	return;
    }

    if (!SV_SetPlayer())
	return;

    Info_Print(host_client->userinfo);
}

/*
================
SV_Gamedir

Sets the fake *gamedir to a different directory.
================
*/
void
SV_Gamedir(void)
{
    const char *dir;

    if (Cmd_Argc() == 1) {
	Con_Printf("Current *gamedir: %s\n",
		   Info_ValueForKey(svs.info, "*gamedir"));
	return;
    }

    if (Cmd_Argc() != 2) {
	Con_Printf("Usage: sv_gamedir <newgamedir>\n");
	return;
    }

    dir = Cmd_Argv(1);

    if (strstr(dir, "..") || strstr(dir, "/")
	|| strstr(dir, "\\") || strstr(dir, ":")) {
	Con_Printf("*Gamedir should be a single filename, not a path\n");
	return;
    }

    Info_SetValueForStarKey(svs.info, "*gamedir", dir, MAX_SERVERINFO_STRING);
}

/*
================
SV_Floodport_f

Sets the gamedir and path to a different directory.
================
*/

void
SV_Floodprot_f(void)
{
    int arg1, arg2, arg3;

    if (Cmd_Argc() == 1) {
	if (fp_messages) {
	    Con_Printf
		("Current floodprot settings: \nAfter %d msgs per %d seconds, silence for %d seconds\n",
		 fp_messages, fp_persecond, fp_secondsdead);
	    return;
	} else
	    Con_Printf("No floodprots enabled.\n");
    }

    if (Cmd_Argc() != 4) {
	Con_Printf
	    ("Usage: floodprot <# of messages> <per # of seconds> <seconds to silence>\n");
	Con_Printf
	    ("Use floodprotmsg to set a custom message to say to the flooder.\n");
	return;
    }

    arg1 = atoi(Cmd_Argv(1));
    arg2 = atoi(Cmd_Argv(2));
    arg3 = atoi(Cmd_Argv(3));

    if (arg1 <= 0 || arg2 <= 0 || arg3 <= 0) {
	Con_Printf("All values must be positive numbers\n");
	return;
    }

    if (arg1 > 10) {
	Con_Printf("Can only track up to 10 messages.\n");
	return;
    }

    fp_messages = arg1;
    fp_persecond = arg2;
    fp_secondsdead = arg3;
}

void
SV_Floodprotmsg_f(void)
{
    if (Cmd_Argc() == 1) {
	Con_Printf("Current msg: %s\n", fp_msg);
	return;
    } else if (Cmd_Argc() != 2) {
	Con_Printf("Usage: floodprotmsg \"<message>\"\n");
	return;
    }
    sprintf(fp_msg, "%s", Cmd_Argv(1));
}

/*
================
SV_Gamedir_f

Sets the gamedir and path to a different directory.
================
*/
char gamedirfile[MAX_OSPATH];
void
SV_Gamedir_f(void)
{
    const char *dir;

    if (Cmd_Argc() == 1) {
	Con_Printf("Current gamedir: %s\n", com_gamedir);
	return;
    }

    if (Cmd_Argc() != 2) {
	Con_Printf("Usage: gamedir <newdir>\n");
	return;
    }

    dir = Cmd_Argv(1);

    if (strstr(dir, "..") || strstr(dir, "/")
	|| strstr(dir, "\\") || strstr(dir, ":")) {
	Con_Printf("Gamedir should be a single filename, not a path\n");
	return;
    }

    COM_Gamedir(dir);
    Info_SetValueForStarKey(svs.info, "*gamedir", dir, MAX_SERVERINFO_STRING);
}

/*
==================
SV_InitOperatorCommands
==================
*/
void
SV_InitOperatorCommands(void)
{
    if (COM_CheckParm("-cheats")) {
	sv_allow_cheats = true;
	Info_SetValueForStarKey(svs.info, "*cheats", "ON",
				MAX_SERVERINFO_STRING);
    }

    Cmd_AddCommand("kick", SV_Kick_f);
    Cmd_AddCommand("status", SV_Status_f);

    Cmd_AddCommand("map", SV_Map_f);
    Cmd_SetCompletion("map", SV_Map_Arg_f);

    Cmd_AddCommand("setmaster", SV_SetMaster_f);

    Cmd_AddCommand("say", SV_ConSay_f);
    Cmd_AddCommand("heartbeat", SV_Heartbeat_f);
    Cmd_AddCommand("quit", SV_Quit_f);
    Cmd_AddCommand("god", SV_God_f);
    Cmd_AddCommand("give", SV_Give_f);
    Cmd_AddCommand("noclip", SV_Noclip_f);
    Cmd_AddCommand("serverinfo", SV_Serverinfo_f);
    Cmd_AddCommand("localinfo", SV_Localinfo_f);
    Cmd_AddCommand("user", SV_User_f);
    Cmd_AddCommand("gamedir", SV_Gamedir_f);
    Cmd_AddCommand("sv_gamedir", SV_Gamedir);
    Cmd_AddCommand("floodprot", SV_Floodprot_f);
    Cmd_AddCommand("floodprotmsg", SV_Floodprotmsg_f);

    cl_warncmd.value = 1;
}
