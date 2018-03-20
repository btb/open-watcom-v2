/****************************************************************************
*
*                            Open Watcom Project
*
*    Portions Copyright (c) 1983-2002 Sybase, Inc. All Rights Reserved.
*
*  ========================================================================
*
*    This file contains Original Code and/or Modifications of Original
*    Code as defined in and that are subject to the Sybase Open Watcom
*    Public License version 1.0 (the 'License'). You may not use this file
*    except in compliance with the License. BY USING THIS FILE YOU AGREE TO
*    ALL TERMS AND CONDITIONS OF THE LICENSE. A copy of the License is
*    provided with the Original Code and Modifications, and is also
*    available at www.sybase.com/developer/opensource.
*
*    The Original Code and all software distributed under the License are
*    distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
*    EXPRESS OR IMPLIED, AND SYBASE AND ALL CONTRIBUTORS HEREBY DISCLAIM
*    ALL SUCH WARRANTIES, INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF
*    MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR
*    NON-INFRINGEMENT. Please see the License for the specific language
*    governing rights and limitations under the License.
*
*  ========================================================================
*
* Description:  WHEN YOU FIGURE OUT WHAT THIS FILE DOES, PLEASE
*               DESCRIBE IT HERE!
*
****************************************************************************/


#include <dos.h>
#include "dbgdefn.h"
#include "dbgdata.h"
#include "kbio.h"
#include "dbgmem.h"
#include "dbgscrn.h"
#include "dbgerr.h"
#include "stdui.h"
#include "dosscrn.h"
#include "tinyio.h"
#include "uidbg.h"
#include "dbgcmdln.h"
#include "dbglkup.h"


#define _NBPARAS( bytes )       ((bytes + 15UL) / 16)

#define TstMono()   ChkCntrlr( VIDMONOINDXREG )
#define TstColour() ChkCntrlr( VIDCOLRINDXREG )

typedef union {
    struct {
        addr32_off      offset;
        addr_seg        segment;
    }           s;
    unsigned_32 a;
} memptr;

extern unsigned MouseStateSize();
#pragma aux MouseStateSize =                                    \
0X29 0XDB       /* sub    bx,bx                         */      \
0XB8 0X15 0X00  /* mov    ax,0015                       */      \
0XCD 0X33       /* int    33                            */      \
                value [bx]                                    \
                modify [ax];


extern void MouseStateSave();
#pragma aux MouseSaveState =                                    \
0XB8 0X16 0X00  /* mov    ax,0016                       */      \
0XCD 0X33       /* int    33                            */      \
                parm [es] [dx]                              \
                modify [ax];


extern void MouseStateRestore();
#pragma aux MouseRestoreState =                                 \
0XB8 0X17 0X00  /* mov    ax,0017                       */      \
0XCD 0X33       /* int    33                            */      \
                parm [es] [dx]                              \
                modify [ax];


extern unsigned BIOSDevCombCode();
#pragma aux BIOSDevCombCode =                                   \
0X55            /* push   bp                            */      \
0XB8 0X00 0X1A  /* mov    ax,1a00                       */      \
0XCD 0X10       /* int    10                            */      \
0X3C 0X1A       /* cmp    al,1a                         */      \
0X74 0X02       /* jz     *+2                           */      \
0X29 0XDB       /* sub    bx,bx                         */      \
0X5D            /* pop    bp                            */      \
                value   [bx]                                  \
                modify  [ax];


extern void DoRingBell( void );
#pragma aux DoRingBell =                                        \
0X55            /* push   bp                            */      \
0XB8 0X07 0X0E  /* mov    ax,0E07                       */      \
0XCD 0X10       /* int    10                            */      \
0X5D            /* pop    bp                            */      \
        parm caller [ax]                                      \
        modify [bx];


extern void             WndDirty( void );

char                    ActFontTbls;  /* assembly file needs access */

static flip_types       FlipMech;
static mode_types       ScrnMode;
static bool             OnAlt;
static char             StrtRows;
static char             StrtPoints;
static char             SavPoints;
static addr_seg         SwapSeg;
static char             StrtMode;
static unsigned char    StrtAttr;
static char             StrtSwtchs;
static int              StrtCurTyp;
static int              SavCurTyp;
static int              SavCurPos;
static char             SaveMode;
static char             SavePage;
static unsigned char    SaveSwtchs;
static unsigned int     VIDPort;
static unsigned int     PageSize;
static unsigned int     CurOffst;
static unsigned int     RegCur;
static unsigned int     InsCur;
static unsigned int     NoCur;
static unsigned char    DbgBiosMode;
static unsigned char    DbgChrSet;
static unsigned char    DbgRows;
static unsigned int     PgmMouse;
static unsigned int     DbgMouse;
static display_config   HWDisplay;
static unsigned char    OldRow;
static unsigned char    OldCol;
static CURSOR_TYPE      OldTyp;

static adapter_type             ColourAdapters[] = {
    #define pick_disp(e,t) t,
        DISP_TYPES()
    #undef pick_disp
};

static const char               ScreenOptNameTab[] = {
    #define pick_opt(e,t) t "\0"
        SCREEN_OPTS()
    #undef pick_opt
};


void Ring_Bell( void )
{
    DoRingBell();
}

static bool ChkCntrlr( unsigned port )
{
    unsigned char   curr;
    bool            rtrn;

    curr = VIDGetRow( port );
    VIDSetRow( port, 0x5a );
    VIDWait();
    VIDWait();
    VIDWait();
    rtrn = ( VIDGetRow( port ) == 0x5a );
    VIDSetRow( port, curr );
    return( rtrn );
}


static void DoSetMode( unsigned char mode )
{
    unsigned char   equip;

    equip = StrtSwtchs & ~0x30;
    switch( mode & 0x7f ) {
    case 0x7:
    case 0xf:
        equip |= 0x30;
        break;
    default:
        equip |= 0x20;
        break;
    }
    BIOSData( BD_EQUIP_LIST, unsigned char ) = equip;
    BIOSSetMode( mode );
}


static void GetDispConfig( void )
{
    signed long         info;
    unsigned char       colour;
    unsigned char       memory;
    unsigned char       swtchs;
    unsigned char       curr_mode;
    hw_display_type     temp;
    unsigned            dev_config;

    dev_config = BIOSDevCombCode();
    HWDisplay.active = dev_config & 0xff;
    HWDisplay.alt = (dev_config >> 8) & 0xff;
    if( HWDisplay.active != DISP_NONE )
        return;
    /* have to figure it out ourselves */
    curr_mode = BIOSGetMode() & 0x7f;
    info = BIOSEGAInfo();
    memory = info;
    colour = info >> 8;
    swtchs = info >> 16;
    if( swtchs < 12 && memory <= 3 && colour <= 1 ) {
        /* we have an EGA */
        if( colour == 0 ) {
            HWDisplay.active = DISP_EGA_COLOUR;
            if( TstMono() ) {
                HWDisplay.alt = DISP_MONOCHROME;
            }
        } else {
            HWDisplay.active = DISP_EGA_MONO;
            if( TstColour() ) {
                HWDisplay.alt = DISP_CGA;
            }
        }
        if( HWDisplay.active == DISP_EGA_COLOUR && ( curr_mode == 7 || curr_mode == 15 )
         || HWDisplay.active == DISP_EGA_MONO && ( curr_mode != 7 && curr_mode != 15 ) ) {
            /* EGA is not the active display */

            temp = HWDisplay.active;
            HWDisplay.active = HWDisplay.alt;
            HWDisplay.alt = temp;
        }
        return;
    }
    if( TstMono() ) {
        /* have a monochrome display */
        HWDisplay.active = DISP_MONOCHROME;
        if( TstColour() ) {
            if( curr_mode != 7 ) {
                HWDisplay.active = DISP_CGA;
                HWDisplay.alt    = DISP_MONOCHROME;
            } else {
                HWDisplay.alt    = DISP_CGA;
            }
        }
        return;
    }
    /* only thing left is a single CGA display */
    HWDisplay.active = DISP_CGA;
}


static bool ChkForColour( hw_display_type display )
{
    if( ColourAdapters[display] == ADAPTER_COLOUR ) {
        ScrnMode = MD_COLOUR;
        return( true );
    }
    return( false );
}


static void SwapActAlt( void )
{
    hw_display_type     temp;

    temp = HWDisplay.active;
    HWDisplay.active = HWDisplay.alt;
    HWDisplay.alt = temp;
    OnAlt = true;
}

static bool ChkColour( void )
{
    if( ChkForColour( HWDisplay.active ) )
        return( true );
    if( ChkForColour( HWDisplay.alt    ) ) {
        SwapActAlt();
        return( true );
    }
    return( false );
}


static bool ChkForMono( hw_display_type display )
{
    if( ColourAdapters[display] == ADAPTER_MONO ) {
        ScrnMode = MD_MONO;
        return( true );
    }
    return( false );
}


static bool ChkMono( void )
{
    if( ChkForMono( HWDisplay.active ) )
        return( true );
    if( ChkForMono( HWDisplay.alt    ) ) {
        SwapActAlt();
        return( true );
    }
    return( false );
}


static bool ChkForEGA( hw_display_type display )
{
    switch( display ) {
    case DISP_EGA_COLOUR:
    case DISP_VGA_COLOUR:
        ScrnMode = MD_EGA;
        return( true );
    case DISP_EGA_MONO:
    case DISP_VGA_MONO:
        ScrnMode = MD_EGA;
        return( true );
    }
    return( false );
}


static bool ChkEGA( void )
{
    if( ChkForEGA( HWDisplay.active ) )
        return( true );
    if( ChkForEGA( HWDisplay.alt    ) ) {
        SwapActAlt();
        return( true );
    }
    return( false );
}


static void GetDefault( void )
{
    if( StrtMode == 0x07 || StrtMode == 0x0f ) {
        if( FlipMech == FLIP_TWO ) {
            if( !ChkColour() ) {
                FlipMech = FLIP_SWAP;
                ChkMono();
            }
        } else {
            ChkMono();
        }
    } else {
        if( FlipMech == FLIP_TWO ) {
            if( !ChkMono() ) {
                FlipMech = FLIP_PAGE;
                ChkColour();
            }
        } else {
            ChkColour();
        }
    }
}


static void ChkPage( void )
{
    switch( ScrnMode ) {
    case MD_MONO:
        FlipMech = FLIP_SWAP;
        break;
    case MD_EGA:
    case MD_COLOUR:
        break;
    default:
        FlipMech = FLIP_SWAP; /* for now */
        break;
    }
}


static void ChkTwo( void )
{
    if( HWDisplay.alt == DISP_NONE ) {
        FlipMech = FLIP_PAGE;
        ChkPage();
    }
}


static void SetChrSet( unsigned set )
{
    if( set != USER_CHR_SET ) {
        BIOSEGAChrSet( set );
    }
}


static unsigned GetChrSet( unsigned char rows )
{
    if( rows >= 43 )
        return( DOUBLE_DOT_CHR_SET );
    if( rows >= 28 )
        return( COMPRESSED_CHR_SET );
    return( USER_CHR_SET );
}


static void SetEGA_VGA( int double_rows )
{
    if( ScrnMode == MD_EGA ) {
        DbgRows = double_rows;
        DbgChrSet = DOUBLE_DOT_CHR_SET;
    } else if( FlipMech == FLIP_SWAP ) {
        DbgChrSet = GetChrSet( BIOSGetRows() );
        switch( DbgChrSet ) {
        case USER_CHR_SET:
            DbgRows = 25;
            break;
        case COMPRESSED_CHR_SET:
            DbgRows = 28;
            break;
        case DOUBLE_DOT_CHR_SET:
            DbgRows = double_rows;
            break;
        }
    } else {
        DbgRows = BIOSGetRows();
        DbgChrSet = USER_CHR_SET;
    }
}


static void SetMonitor( void )
{
    DbgChrSet = USER_CHR_SET;
    switch( HWDisplay.active ) {
    case DISP_MONOCHROME:
        DbgBiosMode = 7;
        break;
    case DISP_CGA:
    case DISP_PGA:              /* just guessing here */
    case DISP_MODEL30_MONO:
    case DISP_MODEL30_COLOUR:
        if( StrtMode == 0x2 && !OnAlt ) {
            DbgBiosMode = 2;
        } else {
            DbgBiosMode = 3;
        }
        break;
    case DISP_EGA_MONO:
        DbgBiosMode = 7;
        SetEGA_VGA( 43 );
        break;
    case DISP_EGA_COLOUR:
        DbgBiosMode = 3;
        SetEGA_VGA( 43 );
        break;
    case DISP_VGA_MONO:
    case DISP_VGA_COLOUR:
        if( StrtMode == 0x7 && !OnAlt ) {
            DbgBiosMode = 7;
        } else {
            DbgBiosMode = 3;
        }
        SetEGA_VGA( 50 );
        break;
    }
    if( DbgRows == 0 )
        DbgRows = 25;
    if( DbgBiosMode == 7 ) {
        VIDPort = VIDMONOINDXREG;
    } else {
        VIDPort = VIDCOLRINDXREG;
    }
    if( ((StrtMode & 0x7f) == DbgBiosMode) && (StrtRows == DbgRows) ) {
        PageSize = BIOSData( BD_REGEN_LEN, unsigned short );    /* get size from BIOS */
    } else {
        PageSize = (DbgRows == 25) ? 4096 : ( DbgRows * (80 * 2) + 256 );
    }
}


static void SaveBIOSSettings( void )
{
    SaveSwtchs = BIOSData( BD_EQUIP_LIST, unsigned char );
    SaveMode = BIOSGetMode();
    SavePage = BIOSGetPage();
    SavCurPos = BIOSGetCurPos( SavePage );
    SavCurTyp = BIOSGetCurTyp( SavePage );

    if( SavCurTyp == CGA_CURSOR_ON && SaveMode == 0x7 ) {
        /* screwy hercules card lying about cursor type */
        SavCurTyp = MON_CURSOR_ON;
    }

    switch( HWDisplay.active ) {
    case DISP_EGA_MONO:
    case DISP_EGA_COLOUR:
    case DISP_VGA_MONO:
    case DISP_VGA_COLOUR:
        SavPoints = BIOSGetPoints();
        break;
    default:
        SavPoints = 8;
        break;
    }
}


/*
 * ConfigScreen -- figure out screen configuration we're going to use.
 */

unsigned ConfigScreen( void )
{
    OnAlt = false;

    GetDispConfig();

    SaveBIOSSettings();

    StrtPoints = SavPoints;
    StrtCurTyp = SavCurTyp;
    StrtSwtchs = SaveSwtchs;
    StrtMode   = SaveMode;
    if( StrtMode < 4 || StrtMode == 7 ) {
        StrtAttr = BIOSGetAttr( SavePage ) & 0x7f;
    } else {
        StrtAttr = 0;
    }
    switch( HWDisplay.active ) {
    case DISP_EGA_MONO:
    case DISP_EGA_COLOUR:
    case DISP_VGA_MONO:
    case DISP_VGA_COLOUR:
        StrtRows = BIOSGetRows();
        break;
    }


    /* get adapter to use */
    switch( ScrnMode ) {
    case MD_HERC: /* temp */
    case MD_DEFAULT:
        GetDefault();
        break;
    case MD_MONO:
        if( !ChkMono() )
            GetDefault();
        break;
    case MD_COLOUR:
        if( !ChkColour() )
            GetDefault();
        break;
    case MD_EGA:
        if( !ChkEGA() )
            GetDefault();
        break;
    }
    /* get flip mechanism to use */
    switch( FlipMech ) {
    case FLIP_PAGE:
        ChkPage();
        break;
    case FLIP_TWO:
        ChkTwo();
        break;
    }
    SetMonitor();
    BIOSSetCurTyp( StrtCurTyp );
    return( PageSize );
}


static bool SetMode( unsigned char mode )
{
    if( (BIOSGetMode() & 0x7f) == (mode & 0x7f) )
        return( false );
    DoSetMode( mode );
    return( true );
}


static void SetRegenClear( void )
{
    BIOSData( BD_VID_CTRL1, unsigned char ) = (BIOSData( BD_VID_CTRL1, unsigned char ) & 0x7f) | (SaveMode & 0x80);
}


static unsigned RegenSize( void )
{
    unsigned    regen_size;

    switch( HWDisplay.active ) {
    case DISP_MONOCHROME:
        regen_size = (DbgRows * (80*2) + 0x3ff) & ~0x3ff;
        break;
    case DISP_CGA:
    case DISP_PGA:
    case DISP_MODEL30_MONO:
    case DISP_MODEL30_COLOUR:
        regen_size = (DbgRows * (80*2) + 0x3ff) & ~0x3ff;
        regen_size *= 4;
        break;
    case DISP_VGA_MONO:
    case DISP_VGA_COLOUR:
    case DISP_EGA_COLOUR:
    case DISP_EGA_MONO:
        regen_size = PageSize*2 + 8*1024;
        break;
    }
    return( regen_size );
}


static void SetupEGA( void )
{
    _disablev( VIDPort + 6 );
    _seq_write( SEQ_MEM_MODE, MEM_NOT_ODD_EVEN );
    _graph_write( GRA_MISC, MIS_A000_64 | MIS_GRAPH_MODE );
    _graph_write( GRA_ENABLE_SR, 0 );
    _graph_write( GRA_DATA_ROT, ROT_UNMOD | 0 );
    _graph_write( GRA_GRAPH_MODE, GRM_EN_ROT );
}

static void SwapSave( void )
{
    switch( HWDisplay.active ) {
    case DISP_VGA_MONO:
    case DISP_VGA_COLOUR:
        _vidstatesave( VID_STATE_SWAP, SwapSeg, PageSize * 2 + 8*1024 );
        /* fall through */
    case DISP_EGA_MONO:
    case DISP_EGA_COLOUR:
        SetupEGA();
        _graph_write( GRA_READ_MAP, RMS_MAP_0 );
        movedata( 0xA000, 0, SwapSeg, 0, PageSize );
        _graph_write( GRA_READ_MAP, RMS_MAP_1 );
        movedata( 0xA000, 0, SwapSeg, PageSize, PageSize );
        _graph_write( GRA_READ_MAP, RMS_MAP_2 );
        movedata( 0xA000, 0, SwapSeg, PageSize * 2, 8 * 1024 );
        _graph_write( GRA_READ_MAP, RMS_MAP_0 );
        /* blank regen area (attributes) */
        _seq_write( SEQ_MAP_MASK, MSK_MAP_1 );
        Fillb( 0xA000, 0, 0, PageSize );
        DoSetMode( DbgBiosMode | 0x80 );
        SetChrSet( DbgChrSet );
        break;
    case DISP_MONOCHROME:
        movedata( 0xb000, 0, SwapSeg, 0, RegenSize() );
        SetMode( DbgBiosMode );
        break;
    default:
        movedata( 0xb800, 0, SwapSeg, 0, RegenSize() );
        SetMode( DbgBiosMode );
        break;
    }
}

static unsigned char RestoreEGA_VGA( void )
{
    unsigned char       mode;

    SetupEGA();
    _seq_write( SEQ_MAP_MASK, MSK_MAP_0 );
    movedata( SwapSeg, 0, 0xA000, 0, PageSize );
    _seq_write( SEQ_MAP_MASK, MSK_MAP_1 );
    movedata( SwapSeg, PageSize, 0xA000, 0, PageSize );
    mode = SaveMode & 0x7f;
    if( mode < 4 || mode == 7 ) {
        DoSetMode( SaveMode | 0x80 );
        BIOSCharSet( 0x00, 32, 256, 0, SwapSeg, PageSize*2 );
//        BIOSCharSet( 0x10, SavPoints, 0, 0, 0, 0 );
    } else {
        _seq_write( SEQ_MAP_MASK, MSK_MAP_2 );
        movedata( SwapSeg, PageSize*2, 0xA000, 0, 8*1024 );
        DoSetMode( SaveMode | 0x80 );
    }
    SetRegenClear();
    return( mode );
}

static void SwapRestore( void )
{
    unsigned char       mode;

    switch( HWDisplay.active ) {
    case DISP_EGA_MONO:
    case DISP_EGA_COLOUR:
        mode = RestoreEGA_VGA();
        if( mode < 4 || mode == 7 ) {
            BIOSCharSet( 0x10, SavPoints, 0, 0, 0, 0 );
        }
        _seq_write( SEQ_CHAR_MAP_SEL, ActFontTbls );
        break;
    case DISP_VGA_MONO:
    case DISP_VGA_COLOUR:
        RestoreEGA_VGA();
        _vidstaterestore( VID_STATE_SWAP, SwapSeg, PageSize * 2 + 8*1024 );
        break;
    case DISP_MONOCHROME:
        SetMode( SaveMode );
        movedata( SwapSeg, 0, 0xb000, 0, RegenSize() );
        break;
    default:
        SetMode( SaveMode );
        movedata( SwapSeg, 0, 0xb800, 0, RegenSize() );
        break;
    }
}

static void SaveMouse( unsigned to )
{
    if( to != 0 ) {
        MouseStateSave( SwapSeg, to, (unsigned)( DbgMouse - PgmMouse ) );
    }
}

static void RestoreMouse( unsigned from )
{
    if( from != 0 ) {
        MouseStateRestore( SwapSeg, from, (unsigned)( DbgMouse - PgmMouse ) );
    }
}


static void AllocSave( void )
{
    unsigned    state_size;
    unsigned    mouse_size;
    unsigned    regen_size;
    tiny_ret_t  ret;

    regen_size = 2; /* make mouse swapping detection work */
    if( FlipMech == FLIP_SWAP ) {
        regen_size = RegenSize();
    }
    state_size = _vidstatesize( VID_STATE_SWAP ) * 64;
    mouse_size = _IsOn( SW_USE_MOUSE ) ? MouseStateSize() : 0;
    ret = TinyAllocBlock( _NBPARAS( regen_size + state_size + 2 * mouse_size ) );
    if( ret < 0 )
        StartupErr( "unable to allocate swap area" );
    SwapSeg = ret;
    if( mouse_size != 0 ) {
        PgmMouse = regen_size + state_size;
        DbgMouse = PgmMouse + mouse_size;
    }
}


static void CheckMSMouse( void )
/* check for Microsoft mouse */
{
    memptr          vect;

    vect = RealModeData( 0, MSMOUSE_VECTOR * 4, memptr );
    if( vect.a == 0 || RealModeData( vect.s.segment, vect.s.offset, uint_8 ) == IRET ) {
        _SwitchOff( SW_USE_MOUSE );
    }
}

static void SetCursorTypes( void )
{
    uint_16     scan_lines;

    switch( HWDisplay.active ) {
    case DISP_MONOCHROME:
        RegCur = MON_CURSOR_ON;
        NoCur = NORM_CURSOR_OFF;
        break;
    case DISP_CGA:
    case DISP_PGA:              /* just guessing here */
        RegCur = CGA_CURSOR_ON;
        NoCur = NORM_CURSOR_OFF;
        break;
    case DISP_EGA_MONO:
    case DISP_EGA_COLOUR:
        /* scan lines per character */
        scan_lines = BIOSGetPoints();
        RegCur = ( scan_lines - 1 ) + ( ( scan_lines - 2 ) << 8 );
        NoCur = EGA_CURSOR_OFF;
        break;
    case DISP_MODEL30_MONO:
    case DISP_MODEL30_COLOUR:
    case DISP_VGA_MONO:
    case DISP_VGA_COLOUR:
        RegCur = VIDGetCurTyp( VIDPort );
        NoCur = NORM_CURSOR_OFF;
        break;
    }
    InsCur = CURSOR_REG2INS( RegCur );
}

static void InitScreenMode( void )
{
    CurOffst = 0;
    switch( FlipMech ) {
    case FLIP_SWAP:
        SwapSave();
        SetRegenClear();
        SetMode( DbgBiosMode );
        SetChrSet( DbgChrSet );
        break;
    case FLIP_PAGE:
        SetMode( DbgBiosMode );
        SetChrSet( DbgChrSet );
        SaveBIOSSettings();
        BIOSSetPage( 1 );
        CurOffst = BIOSData( BD_REGEN_LEN, unsigned ) / 2;
        break;
    case FLIP_TWO:
        DoSetMode( DbgBiosMode );
        SetChrSet( DbgChrSet );
        break;
    case FLIP_OVERWRITE:
        SetMode( DbgBiosMode );
        SetChrSet( DbgChrSet );
        SaveBIOSSettings();
        break;
    }
}

/*
 * InitScreen
 */

void InitScreen( void )
{
    CheckMSMouse();
    AllocSave();
    SaveMouse( PgmMouse );
    SaveMouse( DbgMouse );
    InitScreenMode();
    SetCursorTypes();
}


/*
 * UsrScrnMode -- setup the user screen mode
 */

bool UsrScrnMode( void )
{
    char    user_mode;
    bool    usr_vis;

    if( (StrtAttr != 0) && (DbgBiosMode == StrtMode) ) {
        UIData->attrs[ATTR_NORMAL]  = StrtAttr;
        UIData->attrs[ATTR_BRIGHT]  = StrtAttr ^ 0x8;
        UIData->attrs[ATTR_REVERSE] = (StrtAttr&0x07)<<4 | (StrtAttr&0x70)>>4;
    }
    usr_vis = false;
    if( FlipMech == FLIP_TWO ) {
        usr_vis = true;
        SaveMouse( DbgMouse );
        RestoreMouse( PgmMouse );
        user_mode = (DbgBiosMode == 7) ? 3 : 7;
        DoSetMode( user_mode );
        SaveBIOSSettings();
        SaveMouse( PgmMouse );
        RestoreMouse( DbgMouse );
    }
    SaveSwtchs = BIOSData( BD_EQUIP_LIST, unsigned char );
    if( HWDisplay.active == DISP_VGA_COLOUR
     || HWDisplay.active == DISP_VGA_MONO ) {
        UIData->colour = M_VGA;
    }
    if( DbgRows != UIData->height ) {
        UIData->height = DbgRows;
        if( _IsOn( SW_USE_MOUSE ) ) {
            /*
                This is a sideways dive into the UI to get the boundries of
                the mouse cursor properly defined.
            */
            initmouse( INIT_MOUSE );
        }
    }
    return( usr_vis );
}


void DbgScrnMode( void )
{
    if( FlipMech == FLIP_PAGE ) {
        if( SetMode( DbgBiosMode ) ) {
            SetChrSet( DbgChrSet );
            SaveBIOSSettings();
            WndDirty();
        }
        BIOSSetPage( 1 );
    }
}


/*
 * DebugScreen -- swap/page to debugger screen
 */

bool DebugScreen( void )
{
    bool    usr_vis;

    usr_vis = true;
    SaveMouse( PgmMouse );
    SaveBIOSSettings();
    switch( FlipMech ) {
    case FLIP_SWAP:
        SwapSave();
        BIOSSetPage( 0 );
        WndDirty();
        usr_vis = false;
        break;
    case FLIP_PAGE:
        if( SetMode( DbgBiosMode ) ) {
            SetChrSet( DbgChrSet );
            SaveBIOSSettings();
            WndDirty();
        }
        BIOSSetPage( 1 );
        usr_vis = false;
        break;
    case FLIP_OVERWRITE:
        if( SetMode( DbgBiosMode ) ) {
            SetChrSet( DbgChrSet );
            SaveBIOSSettings();
        }
        WndDirty();
        usr_vis = false;
        break;
    }
    RestoreMouse( DbgMouse );
    uiswap();
    return( usr_vis );
}


/*
 * UserScreen -- swap/page to user screen
 */

bool UserScreen( void )
{
    bool    dbg_vis;

    dbg_vis = true;
    uiswap();
    SaveMouse( DbgMouse );
    switch( FlipMech ) {
    case FLIP_SWAP:
        SwapRestore();
        dbg_vis = false;
        break;
    case FLIP_PAGE:
        dbg_vis = false;
        break;
    }
    BIOSSetPage( SavePage );
    BIOSSetCurTyp( SavCurTyp );
    BIOSSetCurPos( SavCurPos, SavePage );
    BIOSData( BD_EQUIP_LIST, unsigned char ) = SaveSwtchs;
    RestoreMouse( PgmMouse );
    return( dbg_vis );
}


static void ReInitScreen( void )
{
    RestoreMouse( PgmMouse );
    BIOSData( BD_EQUIP_LIST, unsigned char ) = StrtSwtchs;
    BIOSSetMode( StrtMode );
    switch( StrtMode & 0x7f ) {
    case 0x0:
    case 0x1:
    case 0x2:
    case 0x3:
    case 0x7:
        switch( HWDisplay.active ) {
        case DISP_EGA_MONO:
        case DISP_EGA_COLOUR:
        case DISP_VGA_MONO:
        case DISP_VGA_COLOUR:
            SetChrSet( GetChrSet( StrtRows ) );
            break;
        }
        break;
    }
    BIOSSetCurTyp( StrtCurTyp );
    if( StrtAttr != 0 ) {
        BIOSSetAttr( StrtAttr );
    }
}

/*
 * FiniScreen -- finish screen swapping/paging
 */

void FiniScreen( void )
{
    uifini();
    if( SaveSwtchs != StrtSwtchs
     || SaveMode   != StrtMode
     || SavPoints  != StrtPoints
     || FlipMech   == FLIP_OVERWRITE ) {
        ReInitScreen();
    } else {
        UserScreen();
    }
}


/*****************************************************************************\
 *                                                                           *
 *            Replacement routines for User Interface library                *
 *                                                                           *
\*****************************************************************************/


void uiinitcursor( void )
{
}

void uisetcursor( ORD row, ORD col, CURSOR_TYPE typ, int attr )
{
    unsigned    bios_cur_pos;

    if( typ == C_OFF ) {
        uioffcursor();
    } else if( (ScrnState & DBG_SCRN_ACTIVE) && ( VIDPort != 0 ) ) {
        if( row == OldRow && col == OldCol && typ == OldTyp )
            return;
        OldTyp = typ;
        OldRow = row;
        OldCol = col;
        bios_cur_pos = BD_CURPOS;
        if( FlipMech == FLIP_PAGE )
            bios_cur_pos += 2;
        BIOSData( bios_cur_pos + 0, unsigned char ) = OldCol;
        BIOSData( bios_cur_pos + 1, unsigned char ) = OldRow;
        VIDSetPos( VIDPort, CurOffst + row * UIData->width + col );
        VIDSetCurTyp( VIDPort, typ == C_INSERT ? InsCur : RegCur );
    }
}


void uioffcursor( void )
{
    if( (ScrnState & DBG_SCRN_ACTIVE) && ( VIDPort != 0 ) ) {
        OldTyp = C_OFF;
        VIDSetCurTyp( VIDPort, NoCur );
    }
}

void uiswapcursor( void )
{
}

void uifinicursor( void )
{
}

void uirefresh( void )
{
    if( ScrnState & DBG_SCRN_ACTIVE ) {
        _uirefresh();
    }
}

#if 0
#define OFF_SCREEN      200
static ORD OldMouseRow, OldMouseCol = OFF_SCREEN;
static bool MouseOn;
static ATTR OldAttr;


void uimouse( int func )
{
    if( func == 1 ) {
        MouseOn = true;
    } else {
        uisetmouse( 0, OFF_SCREEN );
        MouseOn = false;
    }
}


#pragma aux vertsync =                                          \
                    0xba 0xda 0x03      /* mov dx,3da   */      \
                    0xec                /* in al,dx     */      \
                    0xa8 0x08           /* test al,8    */      \
                    0x74 0xfb           /* jz -5        */      \
                modify [ax dx];
extern void vertsync( void );

static char __far *RegenPos( unsigned row, unsigned col )
{
    char        __far *pos;

    pos = (char __far *)UIData->screen.origin + (row*UIData->screen.increment+col)*2 + 1;
    if( UIData->colour == M_CGA && _IsOff( SW_NOSNOW ) ) {
        /* wait for vertical retrace */
        vertsync();
    }
    return( pos );
}

void uisetmouse( ORD row, ORD col )
{
    char        __far *old;
    char        __far *new;

    if( OldMouseRow == row && OldMouseCol == col )
        return;
    if( OldMouseCol != OFF_SCREEN ) {
        old = RegenPos( OldMouseRow, OldMouseCol );
        *old = OldAttr;
    }
    if( MouseOn ) {
        if( col != OFF_SCREEN ) {
            new = RegenPos( row, col );
            OldAttr = *new;
            *new = (OldAttr & 0x77) ^ 0x77;
        }
        OldMouseRow = row;
        OldMouseCol = col;
    }
}
#endif

static void GetLines( void )
{
    unsigned    num;

    if( HasEquals() ) {
        num = GetValue();
        if( num < 10 || num > 999 ) {
            StartupErr( "lines out of range" );
        }
        /* force a specified number of lines for MDA/CGA systems */
        DbgRows = num;
    }
}

bool ScreenOption( const char *start, unsigned len, int pass )
{
    pass=pass;
    switch( Lookup( ScreenOptNameTab, start, len ) ) {
    case OPT_MONO:
        ScrnMode = MD_MONO;
        GetLines();
        break;
    case OPT_COLOR:
    case OPT_COLOUR:
        ScrnMode = MD_COLOUR;
        GetLines();
        break;
    case OPT_EGA43:
    case OPT_VGA50:
        ScrnMode = MD_EGA;
        break;
    case OPT_OVERWRITE:
        FlipMech = FLIP_OVERWRITE;
        break;
    case OPT_PAGE:
        FlipMech = FLIP_PAGE;
        break;
    case OPT_SWAP:
        FlipMech = FLIP_SWAP;
        break;
    case OPT_TWO:
        FlipMech = FLIP_TWO;
        break;
    default:
        return( false );
    }
    return( true );
}


void ScreenOptInit( void )
{
    ScrnMode = MD_DEFAULT;
    FlipMech = FLIP_TWO;
}
