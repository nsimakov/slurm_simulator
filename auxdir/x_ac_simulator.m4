##*****************************************************************************
#  AUTHOR:
#    Nikolay Simakov <nikolays@buffalo.edu>
#
#  SYNOPSIS:
#    X_AC_SIMULATOR
#
#  DESCRIPTION:
#    Add support for the "--enable-simulator"
#
#    If simulator is enabled, define SLURM_SIMULATOR in config.h ,
#    add -DSLURM_SIMULATOR to CFLAGS and CXXFLAGS, set ENABLE_SIMULATOR
#    for automake and add rt library to LIBS
#    
#
##*****************************************************************************

AC_DEFUN([X_AC_SIMULATOR], [
  AC_MSG_CHECKING([whether or not simulator mode is enabled])
  AC_ARG_ENABLE(
    [simulator],
    AS_HELP_STRING(--enable-simulator,build slurm in simulator mode),
    [ case "$enableval" in
        yes) x_ac_simulator=yes ;;
         no) x_ac_simulator=no ;;
          *) AC_MSG_RESULT([doh!])
             AC_MSG_ERROR([bad value "$enableval" for --enable-simulator]) ;;
      esac
    ]
  )
  if test "$x_ac_simulator" = yes; then
    test "$GCC" = yes && CFLAGS="$CFLAGS -DSLURM_SIMULATOR"
    test "$GXX" = yes && CXXFLAGS="$CXXFLAGS -DSLURM_SIMULATOR"
    LIBS="$LIBS -lrt -lm"
    LDFLAGS="-Wl,-wrap,getpwnam_r -Wl,-wrap,getpwuid_r -Wl,-wrap,getgrnam_r -Wl,-wrap,getgrgid_r $LDFLAGS"
    AC_DEFINE([SLURM_SIMULATOR],[1],
      [Define SLURM_SIMULATOR if you are building slurm in simulator mode.]
    )
  fi
  AM_CONDITIONAL([ENABLE_SIMULATOR], test "$x_ac_simulator" = "yes")
  AC_MSG_RESULT([${x_ac_simulator=no}])

  ]
)
