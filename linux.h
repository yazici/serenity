#pragma once
/// \file linux.h Linux kernel syscalls and error codes
#include "core.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/shm.h>
#include <sys/socket.h>
#include <sys/inotify.h>

/// Linux error code names
enum Error {OK, PERM, NOENT, SRCH, INTR, EIO, NXIO, TOOBIG, NOEXEC, BADF, CHILD, AGAIN, NOMEM, ACCES, FAULT, NOTBLK, BUSY, EXIST, XDEV, NODEV, NOTDIR, ISDIR, INVAL, NFILE, MFILE, NOTTY, TXTBSY, FBIG, NOSPC, SPIPE, ROFS, MLINK, PIPE, DOM, RANGE, DEADLK, NAMETOOLONG, NOLCK, NOSYS, NOTEMPTY, LOOP, WOULDBLOCK, NOMSG, IDRM, CHRNG, L2NSYNC, L3HLT, L3RST, LNRNG, UNATCH, NOCSI, L2HLT, BADE, BADR, XFULL, NOANO, BADRQC, BADSLT, DEADLOCK, EBFONT, NOSTR, NODATA, TIME, NOSR, NONET, NOPKG, REMOTE, NOLINK, ADV, SRMNT, COMM, PROTO, MULTIHO, DOTDOT, BADMSG, OVERFLOW, NOTUNIQ, BADFD, REMCHG, LIBACC, LIBBAD, LIBSCN, LIBMAX, LIBEXEC, ILSEQ, RESTART, STRPIPE, USERS, NOTSOCK, DESTADDRREQ, MSGSIZE, PROTOTYPE, NOPROTOOPT, PROTONOSUPPORT, SOCKTNOSUPPORT, OPNOTSUPP, PFNOSUPPORT, AFNOSUPPORT, ADDRINUSE, ADDRNOTAVAIL, NETDOWN, NETUNREACH, NETRESET, CONNABORTED, CONNRESET, NOBUFS, ISCONN, NOTCONN, SHUTDOWN, TOOMANYREFS, TIMEDOUT, CONNREFUSED, HOSTDOWN, HOSTUNREACH, ALREADY, INPROGRESS, STALE, UCLEAN, NOTNAM, NAVAIL, ISNAM, REMOTEIO, DQUOT, NOMEDIUM, MEDIUMTYPE, CANCELED, NOKEY, KEYEXPIRED, KEYREVOKED, KEYREJECTED, OWNERDEAD, NOTRECOVERABLE, RFKILL, HWPOISON, LAST};
constexpr ref<byte> errno[] = {"OK"_, "PERM"_, "NOENT"_, "SRCH"_, "INTR"_, "IO"_, "NXIO"_, "TOOBIG"_, "NOEXEC"_, "BADF"_, "CHILD"_, "AGAIN"_, "NOMEM"_, "ACCES"_, "FAULT"_, "NOTBLK"_, "BUSY"_, "EXIST"_, "XDEV"_, "NODEV"_, "NOTDIR"_, "ISDIR"_, "INVAL"_, "NFILE"_, "MFILE"_, "NOTTY"_, "TXTBSY"_, "FBIG"_, "NOSPC"_, "SPIPE"_, "ROFS"_, "MLINK"_, "PIPE"_, "DOM"_, "RANGE"_, "DEADLK"_, "NAMETOOLONG"_, "NOLCK"_, "NOSYS"_, "NOTEMPTY"_, "LOOP"_, "WOULDBLOCK"_, "NOMSG"_, "IDRM"_, "CHRNG"_, "L2NSYNC"_, "L3HLT"_, "L3RST"_, "LNRNG"_, "UNATCH"_, "NOCSI"_, "L2HLT"_, "BADE"_, "BADR"_, "XFULL"_, "NOANO"_, "BADRQC"_, "BADSLT"_, "DEADLOCK"_, "EBFONT"_, "NOSTR"_, "NODATA"_, "TIME"_, "NOSR"_, "NONET"_, "NOPKG"_, "REMOTE"_, "NOLINK"_, "ADV"_, "SRMNT"_, "COMM"_, "PROTO"_, "MULTIHO"_, "DOTDOT"_, "BADMSG"_, "OVERFLOW"_, "NOTUNIQ"_, "BADFD"_, "REMCHG"_, "LIBACC"_, "LIBBAD"_, "LIBSCN"_, "LIBMAX"_, "LIBEXEC"_, "ILSEQ"_, "RESTART"_, "STRPIPE"_, "USERS"_, "NOTSOCK"_, "DESTADDRREQ"_, "MSGSIZE"_, "PROTOTYPE"_, "NOPROTOOPT"_, "PROTONOSUPPORT"_, "SOCKTNOSUPPORT"_, "OPNOTSUPP"_, "PFNOSUPPORT"_, "AFNOSUPPORT"_, "ADDRINUSE"_, "ADDRNOTAVAIL"_, "NETDOWN"_, "NETUNREACH"_, "NETRESET"_, "CONNABORTED"_, "CONNRESET"_, "NOBUFS"_, "ISCONN"_, "NOTCONN"_, "SHUTDOWN"_, "TOOMANYREFS"_, "TIMEDOUT"_, "CONNREFUSED"_, "HOSTDOWN"_, "HOSTUNREACH"_, "ALREADY"_, "INPROGRESS"_, "STALE"_, "UCLEAN"_, "NOTNAM"_, "NAVAIL"_, "ISNAM"_, "REMOTEIO"_, "DQUOT"_, "NOMEDIUM"_, "MEDIUMTYPE"_, "CANCELED"_, "NOKEY"_, "KEYEXPIRED"_, "KEYREVOKED"_, "KEYREJECTED"_, "OWNERDEAD"_, "NOTRECOVERABLE"_, "RFKILL"_, "HWPOISON"_};
/// Aborts if \a expr is negative and logs corresponding error code
#define check(expr, message...) ({ long e=(long)expr; if(e<0 && -e<LAST) error(#expr ""_, errno[-e], ##message); e; })
/// Aborts if \a expr is negative and logs corresponding error code (unused result)
#define check_(expr, message...) ({ long unused e=expr; if(e<0 && -e<LAST) error(#expr ""_, errno[-e], ##message); })
