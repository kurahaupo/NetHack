/* NetHack 3.7	nhlua.c	$NHDT-Date: 1580506559 2020/01/31 21:35:59 $  $NHDT-Branch: NetHack-3.7 $:$NHDT-Revision: 1.32 $ */
/*      Copyright (c) 2018 by Pasi Kallinen */
/* NetHack may be freely redistributed.  See license for details. */

#include "hack.h"
#include "dlb.h"

/*
#- include <lua5.3/lua.h>
#- include <lua5.3/lualib.h>
#- include <lua5.3/lauxlib.h>
*/

/*  */

/* lua_CFunction prototypes */
#ifdef DUMPLOG
static int nhl_dump_fmtstr(lua_State *);
#endif /* DUMPLOG */
static int nhl_dnum_name(lua_State *);
static int nhl_stairways(lua_State *);
static int nhl_pushkey(lua_State *);
static int nhl_doturn(lua_State *);
static int nhl_debug_flags(lua_State *);
static int nhl_timer_has_at(lua_State *);
static int nhl_timer_peek_at(lua_State *);
static int nhl_timer_stop_at(lua_State *);
static int nhl_timer_start_at(lua_State *);
static int nhl_test(lua_State *);
static int nhl_getmap(lua_State *);
static char splev_typ2chr(schar);
static int nhl_gettrap(lua_State *);
static int nhl_deltrap(lua_State *);
#if 0
static int nhl_setmap(lua_State *);
#endif
static int nhl_impossible(lua_State *);
static int nhl_pline(lua_State *);
static int nhl_verbalize(lua_State *);
static int nhl_parse_config(lua_State *);
static int nhl_menu(lua_State *);
static int nhl_getlin(lua_State *);
static int nhl_makeplural(lua_State *);
static int nhl_makesingular(lua_State *);
static int nhl_s_suffix(lua_State *);
static int nhl_ing_suffix(lua_State *);
static int nhl_an(lua_State *);
static int nhl_rn2(lua_State *);
static int nhl_random(lua_State *);
static int nhl_level_difficulty(lua_State *);
static void init_nhc_data(lua_State *);
static int nhl_push_anything(lua_State *, int, void *);
static int nhl_meta_u_index(lua_State *);
static int nhl_meta_u_newindex(lua_State *);
static int nhl_u_clear_inventory(lua_State *);
static int nhl_u_giveobj(lua_State *);
static void init_u_data(lua_State *);
static int nhl_set_package_path(lua_State *, const char *);
static int traceback_handler(lua_State *);

static const char *const nhcore_call_names[NUM_NHCORE_CALLS] = {
    "start_new_game",
    "restore_old_game",
    "moveloop_turn",
    "game_exit",
};
static boolean nhcore_call_available[NUM_NHCORE_CALLS];

void
l_nhcore_init(void)
{
    if ((g.luacore = nhl_init()) != 0) {
        if (!nhl_loadlua(g.luacore, "nhcore.lua")) {
            g.luacore = (lua_State *) 0;
        } else {
            int i;

            for (i = 0; i < NUM_NHCORE_CALLS; i++)
                nhcore_call_available[i] = TRUE;
        }
    }
}

void
l_nhcore_done(void)
{
    if (g.luacore) {
        nhl_done(g.luacore);
        g.luacore = 0;
    }
}

void
l_nhcore_call(int callidx)
{
    int ltyp;

    if (callidx < 0 || callidx >= NUM_NHCORE_CALLS
        || !g.luacore || !nhcore_call_available[callidx])
        return;

    lua_getglobal(g.luacore, "nhcore");
    if (!lua_istable(g.luacore, -1)) {
        /*impossible("nhcore is not a lua table");*/
        nhl_done(g.luacore);
        g.luacore = 0;
        return;
    }

    lua_getfield(g.luacore, -1, nhcore_call_names[callidx]);
    ltyp = lua_type(g.luacore, -1);
    if (ltyp == LUA_TFUNCTION) {
        lua_remove(g.luacore, -2); /* nhcore_call_names[callidx] */
        lua_remove(g.luacore, -2); /* nhcore */
        lua_call(g.luacore, 0, 1);
    } else {
        /*impossible("nhcore.%s is not a lua function",
          nhcore_call_names[callidx]);*/
        nhcore_call_available[callidx] = FALSE;
    }
}

DISABLE_WARNING_UNREACHABLE_CODE

void
nhl_error(lua_State *L, const char *msg)
{
    lua_Debug ar;
    char buf[BUFSZ];

    lua_getstack(L, 1, &ar);
    lua_getinfo(L, "lS", &ar);
    Sprintf(buf, "%s (line %d ", msg, ar.currentline);
    Sprintf(eos(buf), "%.*s)",
            /* (max length of ar.short_src is actually LUA_IDSIZE
               so this is overkill for it, but crucial for ar.source) */
            (int) (sizeof buf - (strlen(buf) + sizeof ")")),
            ar.short_src); /* (used to be 'ar.source' here) */
    lua_pushstring(L, buf);
#if 0 /* defined(PANICTRACE) && !defined(NO_SIGNALS) */
    panictrace_setsignals(FALSE);
#endif
    (void) lua_error(L);
    /*NOTREACHED*/
    /* UNREACHABLE_CODE */
}

RESTORE_WARNING_UNREACHABLE_CODE

/* Check that parameters are nothing but single table,
   or if no parameters given, put empty table there */
void
lcheck_param_table(lua_State *L)
{
    int argc = lua_gettop(L);

    if (argc < 1)
        lua_createtable(L, 0, 0);

    /* discard any extra arguments passed in */
    lua_settop(L, 1);

    luaL_checktype(L, 1, LUA_TTABLE);
}

schar
get_table_mapchr(lua_State *L, const char *name)
{
    char *ter;
    xchar typ;

    ter = get_table_str(L, name);
    typ = check_mapchr(ter);
    if (typ == INVALID_TYPE)
        nhl_error(L, "Erroneous map char");
    if (ter)
        free(ter);
    return typ;
}

schar
get_table_mapchr_opt(lua_State *L, const char *name, schar defval)
{
    char *ter;
    xchar typ;

    ter = get_table_str_opt(L, name, emptystr);
    if (name && *ter) {
        typ = check_mapchr(ter);
        if (typ == INVALID_TYPE)
            nhl_error(L, "Erroneous map char");
    } else
        typ = defval;
    if (ter)
        free(ter);
    return typ;
}

short
nhl_get_timertype(lua_State *L, int idx)
{
    static const char *const timerstr[NUM_TIME_FUNCS+1] = {
        "rot-organic", "rot-corpse", "revive-mon", "zombify-mon",
        "burn-obj", "hatch-egg", "fig-transform", "melt-ice", "shrink-glob",
        NULL
    };
    short ret = luaL_checkoption(L, idx, NULL, timerstr);

    if (ret < 0 || ret >= NUM_TIME_FUNCS)
        nhl_error(L, "Unknown timer type");
    return ret;
}

void
nhl_add_table_entry_int(lua_State *L, const char *name, lua_Integer value)
{
    lua_pushstring(L, name);
    lua_pushinteger(L, value);
    lua_rawset(L, -3);
}

void
nhl_add_table_entry_char(lua_State *L, const char *name, char value)
{
    char buf[2];
    Sprintf(buf, "%c", value);
    lua_pushstring(L, name);
    lua_pushstring(L, buf);
    lua_rawset(L, -3);
}

void
nhl_add_table_entry_str(lua_State *L, const char *name, const char *value)
{
    lua_pushstring(L, name);
    lua_pushstring(L, value);
    lua_rawset(L, -3);
}
void
nhl_add_table_entry_bool(lua_State *L, const char *name, boolean value)
{
    lua_pushstring(L, name);
    lua_pushboolean(L, value);
    lua_rawset(L, -3);
}

void
nhl_add_table_entry_region(lua_State *L, const char *name,
                           xchar x1, xchar y1, xchar x2, xchar y2)
{
    lua_pushstring(L, name);
    lua_newtable(L);
    nhl_add_table_entry_int(L, "x1", x1);
    nhl_add_table_entry_int(L, "y1", y1);
    nhl_add_table_entry_int(L, "x2", x2);
    nhl_add_table_entry_int(L, "y2", y2);
    lua_rawset(L, -3);
}

/* converting from special level "map character" to levl location type
   and back. order here is important. */
const struct {
    char ch;
    schar typ;
} char2typ[] = {
                { ' ', STONE },
                { '#', CORR },
                { '.', ROOM },
                { '-', HWALL },
                { '-', TLCORNER },
                { '-', TRCORNER },
                { '-', BLCORNER },
                { '-', BRCORNER },
                { '-', CROSSWALL },
                { '-', TUWALL },
                { '-', TDWALL },
                { '-', TLWALL },
                { '-', TRWALL },
                { '-', DBWALL },
                { '|', VWALL },
                { '+', DOOR },
                { 'A', AIR },
                { 'C', CLOUD },
                { 'S', SDOOR },
                { 'H', SCORR },
                { '{', FOUNTAIN },
                { '\\', THRONE },
                { 'K', SINK },
                { '}', MOAT },
                { 'P', POOL },
                { 'L', LAVAPOOL },
                { 'I', ICE },
                { 'W', WATER },
                { 'T', TREE },
                { 'F', IRONBARS }, /* Fe = iron */
                { 'x', MAX_TYPE }, /* "see-through" */
                { 'B', CROSSWALL }, /* hack: boundary location */
                { 'w', MATCH_WALL }, /* IS_STWALL() */
                { '\0', STONE },
};

schar
splev_chr2typ(char c)
{
    int i;

    for (i = 0; char2typ[i].ch; i++)
        if (c == char2typ[i].ch)
            return char2typ[i].typ;
    return (INVALID_TYPE);
}

schar
check_mapchr(const char *s)
{
    if (s && strlen(s) == 1)
        return splev_chr2typ(s[0]);
    return INVALID_TYPE;
}

static char
splev_typ2chr(schar typ)
{
    int i;

    for (i = 0; char2typ[i].typ < MAX_TYPE; i++)
        if (typ == char2typ[i].typ)
            return char2typ[i].ch;
    return 'x';
}

/* local t = nh.gettrap(x,y); */
/* local t = nh.gettrap({ x = 10, y = 10 }); */
static int
nhl_gettrap(lua_State *L)
{
    int x, y;

    if (!nhl_get_xy_params(L, &x, &y)) {
        nhl_error(L, "Incorrect arguments");
        return 0;
    }

    if (isok(x, y)) {
            struct trap *ttmp = t_at(x,y);

            if (ttmp) {
                lua_newtable(L);

                nhl_add_table_entry_int(L, "tx", ttmp->tx);
                nhl_add_table_entry_int(L, "ty", ttmp->ty);
                nhl_add_table_entry_int(L, "ttyp", ttmp->ttyp);
                nhl_add_table_entry_str(L, "ttyp_name",
                                        get_trapname_bytype(ttmp->ttyp));
                nhl_add_table_entry_bool(L, "tseen", ttmp->tseen);
                nhl_add_table_entry_bool(L, "madeby_u", ttmp->madeby_u);
                switch (ttmp->ttyp) {
                case SQKY_BOARD:
                    nhl_add_table_entry_int(L, "tnote", ttmp->tnote);
                    break;
                case ROLLING_BOULDER_TRAP:
                    nhl_add_table_entry_int(L, "launchx", ttmp->launch.x);
                    nhl_add_table_entry_int(L, "launchy", ttmp->launch.y);
                    nhl_add_table_entry_int(L, "launch2x", ttmp->launch2.x);
                    nhl_add_table_entry_int(L, "launch2y", ttmp->launch2.y);
                    break;
                case PIT:
                case SPIKED_PIT:
                    nhl_add_table_entry_int(L, "conjoined", ttmp->conjoined);
                    break;
                }
                return 1;
            } else
                nhl_error(L, "No trap at location");
        } else
            nhl_error(L, "Coordinates out of range");
    return 0;
}

/* nh.deltrap(x,y); nh.deltrap({ x = 10, y = 15 }); */
static int
nhl_deltrap(lua_State *L)
{
    int x, y;

    if (!nhl_get_xy_params(L, &x, &y)) {
        nhl_error(L, "Incorrect arguments");
        return 0;
    }

    if (isok(x, y)) {
        struct trap *ttmp = t_at(x,y);

        if (ttmp)
            deltrap(ttmp);
    }
    return 0;
}

/* get parameters (XX,YY) or ({ x = XX, y = YY }) or ({ XX, YY }),
   and set the x and y values.
   return TRUE if there are such params in the stack.
 */
boolean
nhl_get_xy_params(lua_State *L, int *x, int *y)
{
    int argc = lua_gettop(L);
    boolean ret = FALSE;

    if (argc == 2) {
        *x = (int) lua_tointeger(L, 1);
        *y = (int) lua_tointeger(L, 2);
        ret = TRUE;
    } else if (argc == 1 && lua_type(L, 1) == LUA_TTABLE) {
        lua_Integer ax, ay;

        ret = get_coord(L, 1, &ax, &ay);
        *x = (int) ax;
        *y = (int) ay;
    }
    return ret;
}

DISABLE_WARNING_UNREACHABLE_CODE

/* local loc = nh.getmap(x,y); */
/* local loc = nh.getmap({ x = 10, y = 35 }); */
static int
nhl_getmap(lua_State *L)
{
    int x, y;

    if (!nhl_get_xy_params(L, &x, &y)) {
        nhl_error(L, "Incorrect arguments");
        return 0;
    }

    if (isok(x, y)) {
            char buf[BUFSZ];
            lua_newtable(L);

            /* FIXME: some should be boolean values */
            nhl_add_table_entry_int(L, "glyph", levl[x][y].glyph);
            nhl_add_table_entry_int(L, "typ", levl[x][y].typ);
            nhl_add_table_entry_str(L, "typ_name",
                                    levltyp_to_name(levl[x][y].typ));
            Sprintf(buf, "%c", splev_typ2chr(levl[x][y].typ));
            nhl_add_table_entry_str(L, "mapchr", buf);
            nhl_add_table_entry_int(L, "seenv", levl[x][y].seenv);
            nhl_add_table_entry_bool(L, "horizontal", levl[x][y].horizontal);
            nhl_add_table_entry_bool(L, "lit", levl[x][y].lit);
            nhl_add_table_entry_bool(L, "waslit", levl[x][y].waslit);
            nhl_add_table_entry_int(L, "roomno", levl[x][y].roomno);
            nhl_add_table_entry_bool(L, "edge", levl[x][y].edge);
            nhl_add_table_entry_bool(L, "candig", levl[x][y].candig);

            nhl_add_table_entry_bool(L, "has_trap", t_at(x,y) ? 1 : 0);

            /* TODO: FIXME: levl[x][y].flags */

            lua_pushliteral(L, "flags");
            lua_newtable(L);

            if (IS_DOOR(levl[x][y].typ)) {
                nhl_add_table_entry_bool(L, "nodoor",
                                         (levl[x][y].flags == D_NODOOR));
                nhl_add_table_entry_bool(L, "broken",
                                         (levl[x][y].flags & D_BROKEN));
                nhl_add_table_entry_bool(L, "isopen",
                                         (levl[x][y].flags & D_ISOPEN));
                nhl_add_table_entry_bool(L, "closed",
                                         (levl[x][y].flags & D_CLOSED));
                nhl_add_table_entry_bool(L, "locked",
                                         (levl[x][y].flags & D_LOCKED));
                nhl_add_table_entry_bool(L, "trapped",
                                         (levl[x][y].flags & D_TRAPPED));
            } else if (IS_ALTAR(levl[x][y].typ)) {
                /* TODO: bits 0, 1, 2 */
                nhl_add_table_entry_bool(L, "shrine",
                                         (levl[x][y].flags & AM_SHRINE));
            } else if (IS_THRONE(levl[x][y].typ)) {
                nhl_add_table_entry_bool(L, "looted",
                                         (levl[x][y].flags & T_LOOTED));
            } else if (levl[x][y].typ == TREE) {
                nhl_add_table_entry_bool(L, "looted",
                                         (levl[x][y].flags & TREE_LOOTED));
                nhl_add_table_entry_bool(L, "swarm",
                                         (levl[x][y].flags & TREE_SWARM));
            } else if (IS_FOUNTAIN(levl[x][y].typ)) {
                nhl_add_table_entry_bool(L, "looted",
                                         (levl[x][y].flags & F_LOOTED));
                nhl_add_table_entry_bool(L, "warned",
                                         (levl[x][y].flags & F_WARNED));
            } else if (IS_SINK(levl[x][y].typ)) {
                nhl_add_table_entry_bool(L, "pudding",
                                         (levl[x][y].flags & S_LPUDDING));
                nhl_add_table_entry_bool(L, "dishwasher",
                                         (levl[x][y].flags & S_LDWASHER));
                nhl_add_table_entry_bool(L, "ring",
                                         (levl[x][y].flags & S_LRING));
            }
            /* TODO: drawbridges, walls, ladders, room=>ICED_xxx */

            lua_settable(L, -3);

            return 1;
    } else {
        /* TODO: return zerorm instead? */
        nhl_error(L, "Coordinates out of range");
        return 0;
    }
}

RESTORE_WARNING_CONDEXPR_IS_CONSTANT

/* impossible("Error!") */
static int
nhl_impossible(lua_State *L)
{
    int argc = lua_gettop(L);

    if (argc == 1)
        impossible("%s", luaL_checkstring(L, 1));
    else
        nhl_error(L, "Wrong args");
    return 0;
}

/* pline("It hits!") */
static int
nhl_pline(lua_State *L)
{
    int argc = lua_gettop(L);

    if (argc == 1)
        pline("%s", luaL_checkstring(L, 1));
    else
        nhl_error(L, "Wrong args");

    return 0;
}

/* verbalize("Fool!") */
static int
nhl_verbalize(lua_State *L)
{
    int argc = lua_gettop(L);

    if (argc == 1)
        verbalize("%s", luaL_checkstring(L, 1));
    else
        nhl_error(L, "Wrong args");

    return 0;
}

/* parse_config("OPTIONS=!color") */
static int
nhl_parse_config(lua_State *L)
{
    int argc = lua_gettop(L);

    if (argc == 1)
        parse_conf_str(luaL_checkstring(L, 1), parse_config_line);
    else
        nhl_error(L, "Wrong args");

    return 0;
}

/* local windowtype = get_config("windowtype"); */
static int
nhl_get_config(lua_State *L)
{
    int argc = lua_gettop(L);

    if (argc == 1) {
        lua_pushstring(L, get_option_value(luaL_checkstring(L, 1)));
        return 1;
    } else
        nhl_error(L, "Wrong args");

    return 0;
}

/*
  str = getlin("What do you want to call this dungeon level?");
 */
static int
nhl_getlin(lua_State *L)
{
    int argc = lua_gettop(L);

    if (argc == 1) {
        const char *prompt = luaL_checkstring(L, 1);
        char buf[BUFSZ];

        getlin(prompt, buf);
        lua_pushstring(L, buf);
        return 1;
    }

    nhl_error(L, "Wrong args");
    return 0;
}

/*
 selected = menu("prompt", default, pickX, { "a" = "option a", "b" = "option b", ...})
 pickX = 0,1,2, or "none", "one", "any" (PICK_X in code)

 selected = menu("prompt", default, pickX,
                { {key:"a", text:"option a"}, {key:"b", text:"option b"}, ... } ) */
static int
nhl_menu(lua_State *L)
{
    static const char *const pickX[] = {"none", "one", "any"}; /* PICK_x */
    int argc = lua_gettop(L);
    const char *prompt;
    const char *defval = "";
    int pick = PICK_ONE, pick_cnt;
    winid tmpwin;
    anything any;
    menu_item *picks = (menu_item *) 0;

    if (argc < 2 || argc > 4) {
        nhl_error(L, "Wrong args");
        return 0;
    }

    prompt = luaL_checkstring(L, 1);
    if (lua_isstring(L, 2))
        defval = luaL_checkstring(L, 2);
    if (lua_isstring(L, 3))
        pick = luaL_checkoption(L, 3, "one", pickX);
    luaL_checktype(L, argc, LUA_TTABLE);

    tmpwin = create_nhwindow(NHW_MENU);
    start_menu(tmpwin, MENU_BEHAVE_STANDARD);

    lua_pushnil(L); /* first key */
    while (lua_next(L, argc) != 0) {
        const char *str = "";
        const char *key = "";

        /* key @ index -2, value @ index -1 */
        if (lua_istable(L, -1)) {
            lua_pushliteral(L, "key");
            lua_gettable(L, -2);
            key = lua_tostring(L, -1);
            lua_pop(L, 1);

            lua_pushliteral(L, "text");
            lua_gettable(L, -2);
            str = lua_tostring(L, -1);
            lua_pop(L, 1);

            /* TODO: glyph, attr, accel, group accel (all optional) */
        } else if (lua_isstring(L, -1)) {
            str = luaL_checkstring(L, -1);
            key = luaL_checkstring(L, -2);
        }

        any = cg.zeroany;
        if (*key)
            any.a_char = key[0];
        add_menu(tmpwin, &nul_glyphinfo, &any, 0, 0, ATR_NONE, str,
                 (*defval && *key && defval[0] == key[0])
                    ? MENU_ITEMFLAGS_SELECTED : MENU_ITEMFLAGS_NONE);

        lua_pop(L, 1); /* removes 'value'; keeps 'key' for next iteration */
    }

    end_menu(tmpwin, prompt);
    pick_cnt = select_menu(tmpwin, pick, &picks);
    destroy_nhwindow(tmpwin);

    if (pick_cnt > 0) {
        char buf[2];
        buf[0] = picks[0].item.a_char;

        if (pick == PICK_ONE && pick_cnt > 1
            && *defval && defval[0] == picks[0].item.a_char)
            buf[0] = picks[1].item.a_char;

        buf[1] = '\0';
        lua_pushstring(L, buf);
        /* TODO: pick any */
    } else {
        char buf[2];
        buf[0] = defval[0];
        buf[1] = '\0';
        lua_pushstring(L, buf);
    }

    return 1;
}

/* makeplural("zorkmid") */
static int
nhl_makeplural(lua_State *L)
{
    int argc = lua_gettop(L);

    if (argc == 1)
        lua_pushstring(L, makeplural(luaL_checkstring(L, 1)));
    else
        nhl_error(L, "Wrong args");

    return 1;
}

/* makesingular("zorkmids") */
static int
nhl_makesingular(lua_State *L)
{
    int argc = lua_gettop(L);

    if (argc == 1)
        lua_pushstring(L, makesingular(luaL_checkstring(L, 1)));
    else
        nhl_error(L, "Wrong args");

    return 1;
}

/* s_suffix("foo") */
static int
nhl_s_suffix(lua_State *L)
{
    int argc = lua_gettop(L);

    if (argc == 1)
        lua_pushstring(L, s_suffix(luaL_checkstring(L, 1)));
    else
        nhl_error(L, "Wrong args");

    return 1;
}

/* ing_suffix("foo") */
static int
nhl_ing_suffix(lua_State *L)
{
    int argc = lua_gettop(L);

    if (argc == 1)
        lua_pushstring(L, ing_suffix(luaL_checkstring(L, 1)));
    else
        nhl_error(L, "Wrong args");

    return 1;
}

/* an("foo") */
static int
nhl_an(lua_State *L)
{
    int argc = lua_gettop(L);

    if (argc == 1)
        lua_pushstring(L, an(luaL_checkstring(L, 1)));
    else
        nhl_error(L, "Wrong args");

    return 1;
}

/* rn2(10) */
static int
nhl_rn2(lua_State *L)
{
    int argc = lua_gettop(L);

    if (argc == 1)
        lua_pushinteger(L, rn2((int) luaL_checkinteger(L, 1)));
    else
        nhl_error(L, "Wrong args");

    return 1;
}

/* random(10);  -- is the same as rn2(10); */
/* random(5,8); -- same as 5 + rn2(8); */
static int
nhl_random(lua_State *L)
{
    int argc = lua_gettop(L);

    if (argc == 1)
        lua_pushinteger(L, rn2((int) luaL_checkinteger(L, 1)));
    else if (argc == 2)
        lua_pushinteger(L, luaL_checkinteger(L, 1) + rn2((int) luaL_checkinteger(L, 2)));
    else
        nhl_error(L, "Wrong args");

    return 1;
}

/* level_difficulty() */
static int
nhl_level_difficulty(lua_State *L)
{
    int argc = lua_gettop(L);
    if (argc == 0) {
        lua_pushinteger(L, level_difficulty());
    }
    else {
        nhl_error(L, "level_difficulty should not have any args");
    }
    return 1;
}

/* get mandatory integer value from table */
int
get_table_int(lua_State *L, const char *name)
{
    int ret;

    lua_getfield(L, -1, name);
    ret = (int) luaL_checkinteger(L, -1);
    lua_pop(L, 1);
    return ret;
}

/* get optional integer value from table */
int
get_table_int_opt(lua_State *L, const char *name, int defval)
{
    int ret = defval;

    lua_getfield(L, -1, name);
    if (!lua_isnil(L, -1)) {
        ret = (int) luaL_checkinteger(L, -1);
    }
    lua_pop(L, 1);
    return ret;
}

char *
get_table_str(lua_State *L, const char *name)
{
    char *ret;

    lua_getfield(L, -1, name);
    ret = dupstr(luaL_checkstring(L, -1));
    lua_pop(L, 1);
    return ret;
}

/* get optional string value from table.
   return value must be freed by caller. */
char *
get_table_str_opt(lua_State *L, const char *name, char *defval)
{
    const char *ret;

    lua_getfield(L, -1, name);
    ret = luaL_optstring(L, -1, defval);
    if (ret) {
        lua_pop(L, 1);
        return dupstr(ret);
    }
    lua_pop(L, 1);
    return NULL;
}

int
get_table_boolean(lua_State *L, const char *name)
{
    static const char *const boolstr[] = {
        "true", "false", "yes", "no", NULL
    };
    /* static const int boolstr2i[] = { TRUE, FALSE, TRUE, FALSE, -1 }; */
    int ltyp;
    int ret = -1;

    lua_getfield(L, -1, name);
    ltyp = lua_type(L, -1);
    if (ltyp == LUA_TSTRING) {
        ret = luaL_checkoption(L, -1, NULL, boolstr);
        /* nhUse(boolstr2i[0]); */
    } else if (ltyp == LUA_TBOOLEAN) {
        ret = lua_toboolean(L, -1);
    } else if (ltyp == LUA_TNUMBER) {
        ret = (int) luaL_checkinteger(L, -1);
        if ( ret < 0 || ret > 1)
            ret = -1;
    }
    lua_pop(L, 1);
    if (ret == -1)
        nhl_error(L, "Expected a boolean");
    return ret;
}

int
get_table_boolean_opt(lua_State *L, const char *name, int defval)
{
    int ret = defval;

    lua_getfield(L, -1, name);
    if (lua_type(L, -1) != LUA_TNIL) {
        lua_pop(L, 1);
        return get_table_boolean(L, name);
    }
    lua_pop(L, 1);
    return ret;
}

/* opts[] is a null-terminated list */
int
get_table_option(lua_State *L,
                 const char *name,
                 const char *defval,
                 const char *const opts[])
{
    int ret;

    lua_getfield(L, -1, name);
    ret = luaL_checkoption(L, -1, defval, opts);
    lua_pop(L, 1);
    return ret;
}

#ifdef DUMPLOG
/* local fname = dump_fmtstr("/tmp/nethack.%n.%d.log"); */
static int
nhl_dump_fmtstr(lua_State *L)
{
    int argc = lua_gettop(L);
    char buf[512];

    if (argc == 1)
        lua_pushstring(L, dump_fmtstr(luaL_checkstring(L, 1), buf, TRUE));
    else
        nhl_error(L, "Expected a string parameter");
    return 1;
}
#endif /* DUMPLOG */

/* local dungeon_name = dnum_name(u.dnum); */
static int
nhl_dnum_name(lua_State *L)
{
    int argc = lua_gettop(L);

    if (argc == 1) {
        lua_Integer dnum = luaL_checkinteger(L, 1);

        if (dnum >= 0 && dnum < g.n_dgns)
            lua_pushstring(L, g.dungeons[dnum].dname);
        else
            lua_pushstring(L, "");
    } else
        nhl_error(L, "Expected an integer parameter");
    return 1;
}

/* local stairs = stairways(); */
static int
nhl_stairways(lua_State *L)
{
    stairway *tmp = g.stairs;
    int i = 1; /* lua arrays should start at 1 */

    lua_newtable(L);

    while (tmp) {

        lua_pushinteger(L, i);
        lua_newtable(L);

        nhl_add_table_entry_bool(L, "up", tmp->up);
        nhl_add_table_entry_bool(L, "ladder", tmp->isladder);
        nhl_add_table_entry_int(L, "x", tmp->sx);
        nhl_add_table_entry_int(L, "y", tmp->sy);
        nhl_add_table_entry_int(L, "dnum", tmp->tolev.dnum);
        nhl_add_table_entry_int(L, "dlevel", tmp->tolev.dlevel);

        lua_settable(L, -3);

        tmp = tmp->next;
        i++;
    }

    return 1;
}

/*
  test( { x = 123, y = 456 } );
*/
static int
nhl_test(lua_State *L)
{
    int x, y;
    char *name, Player[] = "Player";

    /* discard any extra arguments passed in */
    lua_settop(L, 1);

    luaL_checktype(L, 1, LUA_TTABLE);

    x = get_table_int(L, "x");
    y = get_table_int(L, "y");
    name = get_table_str_opt(L, "name", Player);

    pline("TEST:{ x=%i, y=%i, name=\"%s\" }", x,y, name);

    free(name);

    return 1;
}

/* push a key into command queue */
/* nh.pushkey("i"); */
static int
nhl_pushkey(lua_State *L)
{
    int argc = lua_gettop(L);

    if (argc == 1) {
        const char *key = luaL_checkstring(L, 1);

        cmdq_add_key(key[0]);
    }

    return 0;
}

/* do a turn of moveloop, or until g.multi is done if param is true. */
/* nh.doturn(); nh.doturn(true); */
static int
nhl_doturn(lua_State *L)
{
    int argc = lua_gettop(L);
    boolean domulti = FALSE;

    if (argc == 1)
        domulti = lua_toboolean(L, 1);

    do {
        moveloop_core();
    } while (domulti && g.multi);

    return 0;
}

/* set debugging flags. debugging use only, of course. */
/* nh.debug_flags({ mongen = false,
                    hunger = false,
                    overwrite_stairs = true }); */
static int
nhl_debug_flags(lua_State *L)
{
    int val;

    lcheck_param_table(L);

    /* disable monster generation */
    val = get_table_boolean_opt(L, "mongen", -1);
    if (val != -1) {
        iflags.debug_mongen = !(boolean)val; /* value in lua is negated */
        if (iflags.debug_mongen) {
            register struct monst *mtmp, *mtmp2;

            for (mtmp = fmon; mtmp; mtmp = mtmp2) {
                mtmp2 = mtmp->nmon;
                if (DEADMONSTER(mtmp))
                    continue;
                mongone(mtmp);
            }
        }
    }

    /* prevent hunger */
    val = get_table_boolean_opt(L, "hunger", -1);
    if (val != -1) {
        iflags.debug_hunger = !(boolean)val; /* value in lua is negated */
    }

    /* allow overwriting stairs */
    val = get_table_boolean_opt(L, "overwrite_stairs", -1);
    if (val != -1) {
        iflags.debug_overwrite_stairs = (boolean)val;
    }

    return 0;
}

/* does location at x,y have timer? */
/* local has_melttimer = nh.has_timer_at(x,y, "melt-ice"); */
/* local has_melttimer = nh.has_timer_at({x=4,y=7}, "melt-ice"); */
static int
nhl_timer_has_at(lua_State *L)
{
    boolean ret = FALSE;
    short timertype = nhl_get_timertype(L, -1);
    int x, y;
    long when;

    lua_pop(L, 1); /* remove timertype */
    if (!nhl_get_xy_params(L, &x, &y)) {
        nhl_error(L, "nhl_timer_has_at: Wrong args");
        return 0;
    }

    if (isok(x, y)) {
        when = spot_time_expires(x, y, timertype);
        ret = (when > 0L);
    }
    lua_pushboolean(L, ret);
    return 1;
}

/* when does location at x,y timer trigger? */
/* local melttime = nh.peek_timer_at(x,y, "melt-ice"); */
/* local melttime = nh.peek_timer_at({x=5,y=6}, "melt-ice"); */
static int
nhl_timer_peek_at(lua_State *L)
{
    long when = 0L;
    short timertype = nhl_get_timertype(L, -1);
    int x, y;

    lua_pop(L, 1); /* remove timertype */
    if (!nhl_get_xy_params(L, &x, &y)) {
        nhl_error(L, "nhl_timer_peek_at: Wrong args");
        return 0;
    }

    if (timer_is_pos(timertype) && isok(x, y))
        when = spot_time_expires(x, y, timertype);
    lua_pushinteger(L, when);
    return 1;
}

/* stop timer at location x,y */
/* nh.stop_timer_at(x,y, "melt-ice"); */
/* nh.stop_timer_at({x=6,y=8}, "melt-ice"); */
static int
nhl_timer_stop_at(lua_State *L)
{
    short timertype = nhl_get_timertype(L, -1);
    int x, y;

    lua_pop(L, 1); /* remove timertype */
    if (!nhl_get_xy_params(L, &x, &y)) {
        nhl_error(L, "nhl_timer_stop_at: Wrong args");
        return 0;
    }

    if (timer_is_pos(timertype) && isok(x, y))
        spot_stop_timers(x, y, timertype);
    return 0;
}

/* start timer at location x,y */
/* nh.start_timer_at(x,y, "melt-ice", 10); */
static int
nhl_timer_start_at(lua_State *L)
{
    short timertype = nhl_get_timertype(L, -2);
    long when = lua_tointeger(L, -1);
    int x, y;

    lua_pop(L, 2); /* remove when and timertype */
    if (!nhl_get_xy_params(L, &x, &y)) {
        nhl_error(L, "nhl_timer_start_at: Wrong args");
        return 0;
    }

    if (timer_is_pos(timertype) && isok(x, y)) {
        long where = ((long) x << 16) | (long) y;

        spot_stop_timers(x, y, timertype);
        (void) start_timer((long) when, TIMER_LEVEL, MELT_ICE_AWAY,
                           long_to_any(where));
    }
    return 0;
}

static const struct luaL_Reg nhl_functions[] = {
    {"test", nhl_test},

    {"getmap", nhl_getmap},
#if 0
    {"setmap", nhl_setmap},
#endif
    {"gettrap", nhl_gettrap},
    {"deltrap", nhl_deltrap},

    {"has_timer_at", nhl_timer_has_at},
    {"peek_timer_at", nhl_timer_peek_at},
    {"stop_timer_at", nhl_timer_stop_at},
    {"start_timer_at", nhl_timer_start_at},

    {"abscoord", nhl_abs_coord},

    {"impossible", nhl_impossible},
    {"pline", nhl_pline},
    {"verbalize", nhl_verbalize},
    {"menu", nhl_menu},
    {"getlin", nhl_getlin},

    {"makeplural", nhl_makeplural},
    {"makesingular", nhl_makesingular},
    {"s_suffix", nhl_s_suffix},
    {"ing_suffix", nhl_ing_suffix},
    {"an", nhl_an},
    {"rn2", nhl_rn2},
    {"random", nhl_random},
    {"level_difficulty", nhl_level_difficulty},
    {"parse_config", nhl_parse_config},
    {"get_config", nhl_get_config},
    {"get_config_errors", l_get_config_errors},
#ifdef DUMPLOG
    {"dump_fmtstr", nhl_dump_fmtstr},
#endif /* DUMPLOG */
    {"dnum_name", nhl_dnum_name},
    {"stairways", nhl_stairways},
    {"pushkey", nhl_pushkey},
    {"doturn", nhl_doturn},
    {"debug_flags", nhl_debug_flags},
    {NULL, NULL}
};

static const struct {
    const char *name;
    long value;
} nhl_consts[] = {
    { "COLNO",  COLNO },
    { "ROWNO",  ROWNO },
#ifdef DLB
    { "DLB", 1},
#else
    { "DLB", 0},
#endif /* DLB */
    { NULL, 0 },
};

/* register and init the constants table */
static void
init_nhc_data(lua_State *L)
{
    int i;

    lua_newtable(L);

    for (i = 0; nhl_consts[i].name; i++) {
        lua_pushstring(L, nhl_consts[i].name);
        lua_pushinteger(L, nhl_consts[i].value);
        lua_rawset(L, -3);
    }

    lua_setglobal(L, "nhc");
}

static int
nhl_push_anything(lua_State *L, int anytype, void *src)
{
    anything any = cg.zeroany;

    switch (anytype) {
    case ANY_INT: any.a_int = *(int *) src;
        lua_pushinteger(L, any.a_int);
        break;
    case ANY_UCHAR: any.a_uchar = *(uchar *) src;
        lua_pushinteger(L, any.a_uchar);
        break;
    case ANY_SCHAR: any.a_schar = *(schar *) src;
        lua_pushinteger(L, any.a_schar);
        break;
    }
    return 1;
}

static int
nhl_meta_u_index(lua_State *L)
{
    static const struct {
        const char *name;
        void *ptr;
        int type;
    } ustruct[] = {
        { "ux", &(u.ux), ANY_UCHAR },
        { "uy", &(u.uy), ANY_UCHAR },
        { "dx", &(u.dx), ANY_SCHAR },
        { "dy", &(u.dy), ANY_SCHAR },
        { "dz", &(u.dz), ANY_SCHAR },
        { "tx", &(u.tx), ANY_UCHAR },
        { "ty", &(u.ty), ANY_UCHAR },
        { "ulevel", &(u.ulevel), ANY_INT },
        { "ulevelmax", &(u.ulevelmax), ANY_INT },
        { "uhunger", &(u.uhunger), ANY_INT },
        { "nv_range", &(u.nv_range), ANY_INT },
        { "xray_range", &(u.xray_range), ANY_INT },
        { "umonster", &(u.umonster), ANY_INT },
        { "umonnum", &(u.umonnum), ANY_INT },
        { "mh", &(u.mh), ANY_INT },
        { "mhmax", &(u.mhmax), ANY_INT },
        { "mtimedone", &(u.mtimedone), ANY_INT },
        { "dlevel", &(u.uz.dlevel), ANY_SCHAR }, /* actually xchar */
        { "dnum", &(u.uz.dnum), ANY_SCHAR }, /* actually xchar */
        { "uluck", &(u.uluck), ANY_SCHAR },
        { "uhp", &(u.uhp), ANY_INT },
        { "uhpmax", &(u.uhpmax), ANY_INT },
        { "uen", &(u.uen), ANY_INT },
        { "uenmax", &(u.uenmax), ANY_INT },
    };
    const char *tkey = luaL_checkstring(L, 2);
    int i;

    /* FIXME: doesn't really work, eg. negative values for u.dx */
    for (i = 0; i < SIZE(ustruct); i++)
        if (!strcmp(tkey, ustruct[i].name)) {
            return nhl_push_anything(L, ustruct[i].type, ustruct[i].ptr);
        }

    if (!strcmp(tkey, "inventory")) {
        nhl_push_obj(L, g.invent);
        return 1;
    } else if (!strcmp(tkey, "role")) {
        lua_pushstring(L, g.urole.name.m);
        return 1;
    } else if (!strcmp(tkey, "moves")) {
        lua_pushinteger(L, g.moves);
        return 1;
    } else if (!strcmp(tkey, "uhave_amulet")) {
        lua_pushinteger(L, u.uhave.amulet);
        return 1;
    } else if (!strcmp(tkey, "depth")) {
        lua_pushinteger(L, depth(&u.uz));
        return 1;
    }

    nhl_error(L, "Unknown u table index");
    return 0;
}

static int
nhl_meta_u_newindex(lua_State *L)
{
    nhl_error(L, "Cannot set u table values");
    return 0;
}

static int
nhl_u_clear_inventory(lua_State *L UNUSED)
{
    while (g.invent)
        useupall(g.invent);
    return 0;
}

/* Put object into player's inventory */
/* u.giveobj(obj.new("rock")); */
static int
nhl_u_giveobj(lua_State *L)
{
    return nhl_obj_u_giveobj(L);
}

static const struct luaL_Reg nhl_u_functions[] = {
    { "clear_inventory", nhl_u_clear_inventory },
    { "giveobj", nhl_u_giveobj },
    { NULL, NULL }
};

static void
init_u_data(lua_State *L)
{
    lua_newtable(L);
    luaL_setfuncs(L, nhl_u_functions, 0);
    lua_newtable(L);
    lua_pushcfunction(L, nhl_meta_u_index);
    lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, nhl_meta_u_newindex);
    lua_setfield(L, -2, "__newindex");
    lua_setmetatable(L, -2);
    lua_setglobal(L, "u");
}

static int
nhl_set_package_path(lua_State *L, const char *path)
{
    lua_getglobal(L, "package");
    lua_pushstring(L, path);
    lua_setfield(L, -2, "path");
    lua_pop(L, 1);
    return 0;
}

static int
traceback_handler(lua_State *L)
{
    luaL_traceback(L, L, lua_tostring(L, 1), 0);
    /* TODO: call impossible() if fuzzing? */
    return 1;
}

/* read lua code/data from a dlb module or an external file
   into a string buffer and feed that to lua */
boolean
nhl_loadlua(lua_State *L, const char *fname)
{
#define LOADCHUNKSIZE (1L << 13) /* 8K */
    boolean ret = TRUE;
    dlb *fh;
    char *buf = (char *) 0, *bufin, *bufout, *p, *nl, *altfname;
    long buflen, ct, cnt;
    int llret;

    altfname = (char *) alloc(Strlen(fname) + 3); /* 3: '('...')\0' */
    /* don't know whether 'fname' is inside a dlb container;
       if we did, we could choose between "nhdat(<fname>)" and "<fname>"
       but since we don't, compromise */
    Sprintf(altfname, "(%s)", fname);
    fh = dlb_fopen(fname, RDBMODE);
    if (!fh) {
        impossible("nhl_loadlua: Error loading %s", altfname);
        ret = FALSE;
        goto give_up;
    }

    dlb_fseek(fh, 0L, SEEK_END);
    buflen = dlb_ftell(fh);
    dlb_fseek(fh, 0L, SEEK_SET);

    /* extra +1: room to add final '\n' if missing */
    buf = bufout = (char *) alloc(FITSint(buflen + 1 + 1));
    buf[0] = '\0';
    bufin = bufout = buf;

    ct = 0L;
    while (buflen > 0 || ct) {
        /*
         * Semi-arbitrarily limit reads to 8K at a time.  That's big
         * enough to cover the majority of our Lua files in one bite
         * but small enough to fully exercise the partial record
         * handling (when processing the castle's level description).
         *
         * [For an external file (non-DLB), VMS may only be able to
         * read at most 32K-1 at a time depending on the file format
         * in use, and fseek(SEEK_END) only yields an upper bound on
         * the actual amount of data in that situation.]
         */
        if ((cnt = dlb_fread(bufin, 1, min((int)buflen, LOADCHUNKSIZE), fh)) < 0L)
            break;
        buflen -= cnt; /* set up for next iteration, if any */
        if (cnt == 0L) {
            *bufin = '\n'; /* very last line is unterminated? */
            cnt = 1;
        }
        bufin[cnt] = '\0'; /* fread() doesn't do this */

        /* in case partial line was leftover from previous fread */
        bufin -= ct, cnt += ct, ct = 0;

        while (cnt > 0) {
            if ((nl = index(bufin, '\n')) != 0) {
                /* normal case, newline is present */
                ct = (long) (nl - bufin + 1L); /* +1: keep the newline */
                for (p = bufin; p <= nl; ++p)
                    *bufout++ = *bufin++;
                if (*bufin == '\r')
                    ++bufin, ++ct;
                /* update for next loop iteration */
                cnt -= ct;
                ct = 0;
            } else if (strlen(bufin) < LOADCHUNKSIZE) {
                /* no newline => partial record; move unprocessed chars
                   to front of input buffer (bufin portion of buf[]) */
                ct = cnt = (long) (eos(bufin) - bufin);
                for (p = bufout; cnt > 0; --cnt)
                    *p++ = *bufin++;
                *p = '\0';
                bufin = p; /* next fread() populates buf[] starting here */
                /* cnt==0 so inner loop will terminate */
            } else {
                /* LOADCHUNKSIZE portion of buffer already completely full */
                impossible("(%s) line too long", altfname);
                goto give_up;
            }
        }
    }
    *bufout = '\0';
    (void) dlb_fclose(fh);

    llret = luaL_loadbuffer(L, buf, strlen(buf), altfname);
    if (llret != LUA_OK) {
        impossible("luaL_loadbuffer: Error loading %s: %s",
                   altfname, lua_tostring(L, -1));
        ret = FALSE;
        goto give_up;
    } else {
        lua_pushcfunction(L, traceback_handler);
        lua_insert(L, 1);
        if (lua_pcall(L, 0, LUA_MULTRET, -2)) {
            impossible("Lua error: %s", lua_tostring(L, -1));
            ret = FALSE;
            goto give_up;
        }
    }

 give_up:
    if (altfname)
        free((genericptr_t) altfname);
    if (buf)
        free((genericptr_t) buf);
    return ret;
}

lua_State *
nhl_init(void)
{
    lua_State *L = luaL_newstate();

    iflags.in_lua = TRUE;
    luaL_openlibs(L);
    nhl_set_package_path(L, "./?.lua");

    /* register nh -table, and functions for it */
    lua_newtable(L);
    luaL_setfuncs(L, nhl_functions, 0);
    lua_setglobal(L, "nh");

    /* init nhc -table */
    init_nhc_data(L);

    /* init u -table */
    init_u_data(L);

    l_selection_register(L);
    l_register_des(L);

    l_obj_register(L);

    if (!nhl_loadlua(L, "nhlib.lua")) {
        nhl_done(L);
        return (lua_State *) 0;
    }

    return L;
}

void
nhl_done(lua_State *L)
{
    if (L)
        lua_close(L);
    iflags.in_lua = FALSE;
}

boolean
load_lua(const char *name)
{
    boolean ret = TRUE;
    lua_State *L = nhl_init();

    if (!L) {
        ret = FALSE;
        goto give_up;
    }

    if (!nhl_loadlua(L, name)) {
        ret = FALSE;
        goto give_up;
    }

 give_up:
    nhl_done(L);

    return ret;
}

DISABLE_WARNING_CONDEXPR_IS_CONSTANT

const char *
get_lua_version(void)
{
    if (g.lua_ver[0] == 0) {
        lua_State *L = nhl_init();

        if (L) {
            size_t len = 0;
            const char *vs = (const char *) 0;

            /* LUA_VERSION yields "<major>.<minor>" although we check to see
               whether it is "Lua-<major>.<minor>" and strip prefix if so;
               LUA_RELEASE is <LUA_VERSION>.<LUA_VERSION_RELEASE> but doesn't
               get set up as a lua global */
            lua_getglobal(L, "_RELEASE");
            if (lua_isstring(L, -1))
                vs = lua_tolstring (L, -1, &len);
#ifdef LUA_RELEASE
            else
                vs = LUA_RELEASE, len = strlen(vs);
#endif
            if (!vs) {
                lua_getglobal(L, "_VERSION");
                if (lua_isstring(L, -1))
                    vs = lua_tolstring (L, -1, &len);
#ifdef LUA_VERSION
                else
                    vs = LUA_VERSION, len = strlen(vs);
#endif
            }
            if (vs && len < sizeof g.lua_ver) {
                if (!strncmpi(vs, "Lua", 3)) {
                    vs += 3;
                    if (*vs == '-' || *vs == ' ')
                        vs += 1;
                }
                Strcpy(g.lua_ver, vs);
            }
        }
        nhl_done(L);
#ifdef LUA_COPYRIGHT
        if (sizeof LUA_COPYRIGHT <= sizeof g.lua_copyright)
            Strcpy(g.lua_copyright, LUA_COPYRIGHT);
#endif
    }
    return (const char *) g.lua_ver;
}

RESTORE_WARNINGS



