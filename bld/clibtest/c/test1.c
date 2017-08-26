/* OS/2 Startup Test 1  */

#if !defined(QA_MAKE_EXE) && !defined(QA_MAKE_DLL)
#error You must define either of QA_MAKE_EXE or QA_MAKE_DLL
#endif
#if defined(QA_MAKE_EXE) && defined(QA_MAKE_DLL)
#error You can only define one of QA_MAKE_EXE or QA_MAKE_DLL
#endif

#include <stdio.h>
#include <process.h>
#include <dos.h>

#define INCL_DOS
#include <os2.h>

#if defined(QA_MAKE_DLL)

void dll_threadfunc( void* private_data )
{
    static int counter = 0;

    ++counter;
    printf( "DLL threadfunc entered %2d time. This time during %s\n",
            counter, private_data );
}

void do_start_threads( const char* const szMsg )
{
    int i;

    printf( "In DLL do_start_threads called from %s\n", szMsg );
    for( i = 0; i < 10; i++ ) {
        _beginthread( &dll_threadfunc, NULL, 8192, (void*)szMsg );
    }
}

static const char* const rgMsgs[2] =
{
    "Process initialization\n",
    "Process termination\n"
};

unsigned APIENTRY LibMain( unsigned hmod, unsigned termination )
{
    if( termination )
        printf( rgMsgs[1] );
    else
        printf( rgMsgs[0] );
    return( 1 );
}

void __export QA_func1( void )
{
    do_start_threads( "QA_func1" );
    sleep( 1 /* second */); // Let'em die
}

#endif /* QA_MAKE_DLL */

#if defined(QA_MAKE_EXE)

extern void QA_func1( void );

// check that threading works at all in the exe
void exe_threadfunc( void* private_data )
{
    static int counter = 0;

    ++counter;
    printf( ".exe threadfunc entered %2d times.\n", counter );
}

void do_start_threads( void )
{
    int i;

    printf( "in .exe: do_start_threads\n" );
    for( i = 0; i < 10; ++i ) {
        _beginthread( &exe_threadfunc, NULL, 8192, 0 );
    }
}

/* OS/2 Threading Test 2 */

/* Test thread creation - many very short-lived threads in quick
 * succession, and vary thread priority. If there are problems
 * with threads that are either too short-lived or created too
 * quickly after each other, this test should detect them.
 */

static int counter = 0;

static void exe_threadfunc1( void* private_data )
{
    DosSetPriority( PRTYS_THREAD, PRTYC_TIMECRITICAL, 0, 0 );
    ++counter;
}

static void exe_threadfunc2( void* private_data )
{
    DosSetPriority( PRTYS_THREAD, PRTYC_IDLETIME, 0, 0 );
    ++counter;
}

static void do_start_threads1( void )
{
    int i;

    printf( "calling do_start_threads1\n" );
    for( i = 0; i < 100; ++i ) {
        _beginthread( &exe_threadfunc1, NULL, 8192, 0 );
    }
}

static void do_start_threads2( void )
{
    int i;

    printf( "calling do_start_threads2\n" );
    for( i = 0; i < 100; ++i ) {
        _beginthread( &exe_threadfunc2, NULL, 8192, 0 );
    }
}

int main( void )
{
    /* Test 1 */
    do_start_threads();
    sleep( 1 /* second */); // Let'em die
    QA_func1();

    /* Test 2 */
    // First try threads that will exit very quickly
    do_start_threads1();
    sleep( 1 /* second */); // Let'em die
    printf( "threadfunc entered %2d times.\n", counter );
    DosSetPriority( PRTYS_THREAD, PRTYC_TIMECRITICAL, 0, 0 );
    // Next try threads that won't finish before all have been created
    do_start_threads2();
    printf( "threadfunc entered %2d times.\n", counter );
    sleep( 1 /* second */); // Let'em die
    printf( "threadfunc entered %2d times.\n", counter );

    return( 0 );
}

#endif /* QA_MAKE_EXE */
