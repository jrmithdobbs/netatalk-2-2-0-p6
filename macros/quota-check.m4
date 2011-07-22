dnl $Id: quota-check.m4,v 1.6 2005-07-20 23:58:21 didg Exp $
dnl Autoconf macro to check for quota support
dnl FIXME: This is in now way complete.

AC_DEFUN([AC_CHECK_QUOTA], [
	AC_ARG_ENABLE(quota,
	[  --enable-quota           Turn on quota support (default=auto)])

	if test x$enable_quota != xno; then
	QUOTA_LIBS=""
	netatalk_cv_quotasupport="yes"
	AC_CHECK_LIB(rpcsvc, main, [QUOTA_LIBS="-lrpcsvc"])
	AC_CHECK_HEADERS([rpc/rpc.h rpc/pmap_prot.h rpcsvc/rquota.h],[],[
		QUOTA_LIBS=""
		netatalk_cv_quotasupport="no"
		AC_DEFINE(NO_QUOTA_SUPPORT, 1, [Define if quota support should not compiled])
	])
	else
		netatalk_cv_quotasupport="no"
		AC_DEFINE(NO_QUOTA_SUPPORT, 1, [Define if quota support should not compiled])
	fi

	AC_SUBST(QUOTA_LIBS)
])

