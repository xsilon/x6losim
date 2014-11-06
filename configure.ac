dnl Process this file with autoconf to produce a configure script.

AC_PREREQ(2.59)
AC_INIT(x6losim, 1.0)


AC_CANONICAL_SYSTEM
AM_INIT_AUTOMAKE([subdir-objects])

AC_PROG_CXX

AC_SEARCH_LIBS([pthread_create], [pthread], [], [
  AC_MSG_ERROR([unable to find the pthread lib])
])
AC_CHECK_HEADER([pthread.h])


AC_CONFIG_FILES(Makefile src/Makefile)
AC_OUTPUT

