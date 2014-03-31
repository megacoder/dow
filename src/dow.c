/*
 * vim: ts=8 sw=8
 */

#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <limits.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>
#include <errno.h>

typedef	enum		action_e	{
	Action_none	= 0,
	Action_refuse	= 1,
	Action_keep	= 2,
	Action_discard	= 3,
	Action_move	= 4
} action_t;

#define MAXCHUNK	( (size_t) 128 * (size_t) 1024 )
#define	min(x,y)	( (x) < (y) ? (x) : (y) )
#define DIM(a)		( sizeof( (a) ) / sizeof( (a)[0] ) )

static	char *		me = "dow";
static	unsigned	debugLevel;
static	unsigned	nonfatal;
static	unsigned	verbose;
static	unsigned	dont;

static	void
debug(
	unsigned	theLevel,
	char const *	fmt,
	...
)
{
	if( theLevel <= debugLevel )	{
		va_list		ap;

		va_start( ap, fmt );
		vprintf( fmt, ap );
		va_end( ap );
		printf( ".\n" );
	}
}

static	void
error(
	int		e,
	char const *	fmt,
	...
)
{
	va_list		ap;

	fprintf( stderr, "%s: ", me );
	va_start( ap, fmt );
	vfprintf( stderr, fmt, ap );
	va_end( ap );
	if( e )	{
		fprintf(
			stderr,
			"; errno=%d (%s)",
			e,
			strerror( e )
		);
	}
	fprintf( stderr, ".\n" );
	++nonfatal;
}

static	void
usage(
	char const *	fmt,
	...
)
{
	if( fmt )	{
		va_list		ap;

		fprintf( stderr, "%s: ", me );
		va_start( ap, fmt );
		vfprintf( stderr, fmt, ap );
		va_end( ap );
		fprintf( stderr, ".\n" );
	}
	fprintf(
		stderr,
		"usage: %s"
		" [-D]"
		" [-n]"
		" [-v]"
		" [old new]"
		" |"
		" [file1..filen dir]"
		"\n",
		me
	);
}

static	size_t
remain(
	struct stat * const	st,
	size_t const		orig
)
{
	size_t			result;

	result = 0;
	do	{
		result = min( st->st_size - orig, MAXCHUNK );
		debug( 2, "remain returns %lu", (unsigned long) result );
	} while( 0 );
	return( result );
}

static	int
same_content(
	char const * const	srcFn,
	struct stat * const	srcSt,
	char const * const	dstFn
)
{
	int			result;

	result = 1;		/* Guilty until proven innocent		*/
	do	{
		int			src_fd;
		
		src_fd = open( srcFn, O_RDONLY );
		if( src_fd == -1 )	{
			error(
				errno,
				"cannot open '%s' for reading",
				srcFn
			);
			break;
		}
		do	{
			int		dst_fd;
			off_t		offset;

			dst_fd = open( dstFn, O_RDONLY );

			if( dst_fd == -1 )	{
				error(
					errno,
					"cannot open '%s' for reading",
					dstFn
				);
				break;
			}
			for(
				offset = 0;
				offset < srcSt->st_size;
			)	{
				size_t const	gulp = remain(
					srcSt,
					offset
				);
				void *		src_buf;
				void *		dst_buf;

				src_buf = mmap(
					NULL,
					gulp,
					PROT_READ,
					MAP_PRIVATE,
					src_fd,
					offset
				);
				if( src_buf == MAP_FAILED )	{
					error(
						errno,
					"Failed to map src %llu bytes at %llu",
						(unsigned long long) gulp,
						(unsigned long long) offset
					);
					break;
				}
				dst_buf = mmap(
					NULL,
					gulp,
					PROT_READ,
					MAP_PRIVATE,
					dst_fd,
					offset
				);
				if( dst_buf == MAP_FAILED )	{
					error(
						errno,
					"Failed to map dst %llu bytes at %llu",
						(unsigned long long) gulp,
						(unsigned long long) offset
					);
					break;
				} else if( memcmp(
					src_buf,
					dst_buf,
					gulp
				) )	{
					break;
				}
				(void) munmap( src_buf, gulp );
				(void) munmap( dst_buf, gulp );
				offset += gulp;
			}
			close( dst_fd );
		} while( 0 );
		close( src_fd );
	} while( 0 );
	return( result );
}

static	int
are_identical(
	char const * const	srcFn,
	struct stat * const	srcSt,
	char const * const	dstFn
)
{
	int			result;		/* Nonzero if diffs	*/

	result = 0;
	do	{
		struct stat	dstSt;

		/* Does destination file even exist?			*/
		if( stat( dstFn, &dstSt ) )	{
			/* Nope, so they differ by definition		*/
			break;
		}
		/* Are they the same size?				*/
		if( srcSt->st_size != dstSt.st_size )	{
			/* File sizes differ				*/
			break;
		}
		/* Check the file content				*/
		if( !same_content( srcFn, srcSt, dstFn ) )	{
			/* File content differs				*/
			break;
		}
		/* Files same as far as I can tell			*/
		result = 1;
	} while( 0 );
	return( result );
}

static	int
copyfile(
	char const * const	src,
	char const * const	dst
)
{
	int			result;
	int			src_fd;
	int			dst_fd;

	result = -1;
	src_fd = -1;
	dst_fd = -1;
	do	{
		size_t const	Lbuf = 1024 * 256;
		char		buf[ Lbuf ];
		ssize_t		n;

		src_fd = open( src, O_RDONLY );
		if( src_fd == -1 )	{
			error(
				errno,
				"cannot open %s for reading",
				src
			);
			break;
		}
		dst_fd = open( dst, (O_WRONLY|O_CREAT), 0666 );
		if( dst_fd == -1 )	{
			error(
				errno,
				"cannot open %s for writing",
				dst
			);
		}
		while( (n = read( src_fd, buf, Lbuf )) > 0 )	{
			if( write( dst_fd, buf, n ) != n )	{
				error(
					errno,
					"out of space of %s device",
					dst
				);
				break;
			}
		}
		result = 0;
	} while( 0 );
	if( src_fd != -1 )	{
		close( src_fd );
	}
	if( dst_fd != -1 )	{
		close( dst_fd );
	}
	return( result );
}

static	int
process(
	char const * const	srcFn,
	struct stat * const	srcSt,
	char const *		target
)
{
	int			result;

	debug( 2, "process( '%s', ptr, '%s' )", srcFn, target );
	result = -1;
	do	{
		char		newPath[ (PATH_MAX + 1) * 2 ];
		action_t	action;
		struct stat	targetSt;

		action = Action_none;
		/* If destination exists, cannot be same as src		 */
		if( stat( target, &targetSt ) == 0 )	{
			if( S_ISDIR( targetSt.st_mode ) )	{
				/* Target is existing directory		 */
				char const *	base;

				base = strrchr( srcFn, '/' );
				if( base )	{
					++base;
				} else	{
					base = srcFn;
				}
				sprintf( newPath, "%s/%s", target, base );
				target = newPath;
				debug( 3, "new target = |%s|", target );
			}
		}
		if( stat( target, &targetSt ) == 0 )	{
			/* Cannot be same filesystem and inode		*/
			if(
				(srcSt->st_dev == targetSt.st_dev)	&&
				(srcSt->st_ino == targetSt.st_ino)
			)	{
				/* Don't overwrite self			 */
				action = Action_keep;
				debug( 3, "%s same file as %s", srcFn, target );
			} else	{
				action = are_identical( srcFn, srcSt, target ) ?
					Action_discard : Action_refuse;
			}
		} else	{
			action = Action_move;
		}
		switch( action )	{
		default:
			fprintf( stderr, "action=%d??\n", action );
			break;
		case Action_none:
			result = 0;
			break;
		case Action_refuse:
			fprintf(
				stderr,
				"%s: refuse overwrite of %s\n",
				me,
				target
			);
			break;
		case Action_keep:
			if( dont | verbose )	{
				printf( "# No action for %s\n", srcFn );
			}
			result = 0;
			break;
		case Action_discard:
			if( dont | verbose )	{
				printf( "rm %s\n", srcFn );
			}
			if( !dont && unlink( srcFn ) )	{
				error(
					errno,
					"cannot unlink %s",
					srcFn
				);
			} else	{
				result = 0;
			}
			break;
		case Action_move:
			if( dont | verbose )	{
				printf( "mv %s %s\n", srcFn, target );
			}
			if( !dont && rename( srcFn, target ) )	{
				if( errno != EXDEV )	{
					error(
						errno,
						"cannot rename %s to %s",
						srcFn,
						target
					);
				} else	{
					result = copyfile( srcFn, target );
					if( result == 0 )	{
						unlink( srcFn );
					}
				}
			} else	{
				result = 0;
			}
			break;
		}
	} while( 0 );
	return( result );
}

int
main(
	int		argc,
	char * *	argv
)
{
	char *		bp;
	int		c;

	/* Compute our process name					*/
	me = argv[ 0 ];
	if( (bp = strrchr( me, '/' )) != NULL )	{
		me = bp + 1;
	}
	/* Process command line parameters				*/
	opterr = 0;
	while( (c = getopt( argc, argv, "Dnv" )) != EOF )	{
		switch( c )	{
		default:
			usage( "switch -%c not implemented yet", c );
			exit( 1 );
		case '?':
			usage( "unknown switch -%c", optopt );
			exit( 1 );
		case 'D':
			++debugLevel;
			break;
		case 'n':
			dont = 1;
			break;
		case 'v':
			++verbose;
			break;
		}
	}
	/* Process the remainder of the command line			*/
	do	{
		int const		n = (argc - optind);
		char const * const	target = argv[ argc - 1 ];
		struct stat		targetSt;

		if( n < 2 )	{
			usage( "not enough arguments" );
			++nonfatal;
			break;
		}
		if( n > 2 )	{
			if(
				stat( target, &targetSt ) ||
				!S_ISDIR( targetSt.st_mode )
			)	{
				error(
					0,
					"Multi-file mode target not a dir"
				);
				exit( 1 );
				/*NOTREACHED*/
			}
		}
		/* Process command line tokens				*/
		while( optind < (argc-1) )	{
			char const * const	srcFn = argv[ optind++ ];
			struct stat		srcSt;

			/* Sources must be ordinary files		 */
			if( stat( srcFn, &srcSt ) )	{
				error(
					errno,
					"cannot stat '%s'",
					srcFn
				);
				continue;
			}
			if( S_ISDIR( srcSt.st_mode ) )	{
				error(
					0,
					"'%s' is not an ordinary file",
					srcFn
				);
				continue;
			}
			if( process( srcFn, &srcSt, target ) )	{
				++nonfatal;
			}
		}
	} while( 0 );
	return( nonfatal ? 1 : 0 );
}
