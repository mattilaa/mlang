# `mlang-frontend-mla` manual

```roff
.TH MLANG-FRONTEND-MLA 1 "May 2026" "MLang" "User Commands"
.SH NAME
mlang-frontend-mla \- self-hosted MLang implementation of the compiler frontend wrapper
.SH SYNOPSIS
.B mlang-frontend-mla
[\fB\-\-backend\fR \fIPATH\fR] [\fIcompiler and workflow arguments\fR]
.SH DESCRIPTION
.B mlang-frontend-mla
is the self-hosted MLang implementation of the frontend wrapper interface.
Like
.BR mlang-frontend (1) ,
it forwards compiler, test, benchmark, documentation, and package-manager
requests to a backend compiler executable, defaulting to
.BR mlang .
.PP
This command is mainly used in bootstrap and parity flows where the frontend
itself is built from MLang source.
.SH OPTIONS
.TP
.B \-\-backend \fIPATH\fR
Use
.I PATH
as the backend compiler executable instead of
.BR mlang .
.TP
.BR \-h ", " \-\-help
Print the frontend help text.
.TP
.B \-\-version
Print the frontend version and exit.
.PP
All remaining arguments are forwarded as normal MLang compiler, test,
benchmark, documentation, or package-manager requests.
.SH ENVIRONMENT
.TP
.B MLANG_PKG_IMPL
Select the package-manager backend when the
.B pkg
subcommand is used.
.TP
.B MLANG_STDLIB_PATH
Override the standard-library module search path.
.TP
.B MLANG_STDLIB_LIB_PATH
Override the search path used to locate
.BR libmlang_std .
.TP
.B MLANG_PKG_CACHE_KEY
Influence the cache key used for compiled helper tool frontends.
.SH EXAMPLES
.nf
mlang-frontend-mla hello.mla
mlang-frontend-mla --backend /path/to/mlang pkg build
mlang-frontend-mla test tests/
.fi
.SH SEE ALSO
.BR mlang (1),
.BR mlang-pkg (1),
.BR mlang-frontend (1)
```
