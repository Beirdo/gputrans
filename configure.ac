#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.59)
AC_INIT(gputrans, SVN, gjhurlbu@gmail.com)
AC_CONFIG_AUX_DIR(scripts)
AC_CONFIG_SRCDIR(src/main.c)
AM_CONFIG_HEADER(include/config.h)

AC_CANONICAL_SYSTEM

AM_INIT_AUTOMAKE(gputrans, "SVN")


# Checks for programs.
AC_PROG_CC

AC_DEFUN([GJH_CHECK_LIB],
[AC_CHECK_LIB($1, $2)
 if test $ac_cv_lib_$1_$2 = no ; then
    echo "**** Need library lib$1 to compile! ****"
    AC_MSG_ERROR([no lib$1 found])
 fi
])

# Checks for libraries.
GJH_CHECK_LIB(glut, glutInit)
GJH_CHECK_LIB(GLEW, glewInit)
GJH_CHECK_LIB(GL,   glAccum)
GJH_CHECK_LIB(pthread, pthread_create)

# Check version of GLEW >= 1.3.1 (where framebuffer objects got supported)
MIN_GLEW_VERSION='1.3.1'
AC_DEFINE_UNQUOTED( [MIN_GLEW_VERSION], ["$MIN_GLEW_VERSION"], 
           [Minimum libGLEW version required] )
AC_SUBST( MIN_GLEW_VERSION )
AC_MSG_CHECKING(version of libGLEW >= $MIN_GLEW_VERSION)
AC_LANG_PUSH(C)
AC_RUN_IFELSE([AC_LANG_SOURCE([[
#include <stdio.h>
#include <GL/glew.h>

int main( int argc, char **argv )
{
    const unsigned char *versionString;
    int         major, minor, patch;
    int         min_major, min_minor, min_patch;

    glewInit();
    versionString = glewGetString(GLEW_VERSION);
    sscanf( (char *)versionString, "%d.%d.%d", &major, &minor, &patch );
    sscanf( (char *)MIN_GLEW_VERSION, "%d.%d.%d", &min_major, &min_minor, 
                                                  &min_patch );
    printf( "%d.%d.%d   ", major, minor, patch );

    if( major >  min_major )  return( 0 );
    if( major <  min_major )  return( 1 );
    if( minor >  min_minor )  return( 0 );
    if( minor <  min_minor )  return( 1 );
    if( patch >= min_patch )  return( 0 );
    return(1);
}
]])], [AC_MSG_RESULT([OK])], 
  [AC_MSG_RESULT([fail])
   AC_MSG_FAILURE([libGLEW is too old])], 
  [AC_MSG_RESULT[no]])
AC_LANG_POP(C)

AC_ARG_VAR(FFMPEG_CONFIG, [location of the ffmpeg-config command])
AC_PATH_PROG(FFMPEG_CONFIG, ffmpeg-config)

if test -z "${FFMPEG_CONFIG}" ; then
   AC_MSG_ERROR([You need ffmpeg-config (from the ffmpeg libraries)])
fi

AC_MSG_CHECKING(libs to add for proper ffmpeg operation)
FFMPEG_LIBS=`${FFMPEG_CONFIG} --plugin-libs avformat`
AC_MSG_RESULT(${FFMPEG_LIBS})

LIBS="${FFMPEG_LIBS} ${LIBS}"


#GJH_CHECK_LIB(avutil, av_rescale)
#GJH_CHECK_LIB(avcodec, avcodec_open)
#GJH_CHECK_LIB(avformat, av_read_frame)


# Checks for header files.
AC_HEADER_STDC
AC_HEADER_SYS_WAIT
AC_CHECK_HEADERS([fcntl.h inttypes.h stdlib.h string.h sys/time.h syslog.h])
AC_CHECK_HEADERS([unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_HEADER_STDBOOL
AC_C_CONST
AC_TYPE_PID_T
AC_TYPE_SIZE_T
AC_CHECK_DECLS([sys_siglist])
AC_HEADER_TIME
AC_STRUCT_TM

# Checks for library functions.
AC_FUNC_FORK
AC_FUNC_MALLOC
AC_TYPE_SIGNAL
AC_FUNC_STRFTIME
AC_CHECK_FUNCS([atexit gettimeofday localtime_r memmove memset strchr strdup])

AC_OUTPUT([Makefile src/Makefile example/Makefile])
