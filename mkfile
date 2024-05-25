# base/gxshade6.c also needs editing to remove use of the
# vlong , operator on some architectures.
#
# you may need to change this line in gs/base/gshtscr.c:
# //      sample = (int) (value * max_ht_sample) + max_ht_sample;
# to
# 	sample = (vlong)(value * max_ht_sample) + max_ht_sample;

</$objtype/mkfile

BIN=/$objtype/bin

TARG=gs
OFILES=\
	obj/gs.$O\
	`{sed 's#^./obj/(.*)\.o .*#obj/\1.$O#' base/ld.tr >[2] /dev/null | sort}

# The first driver is the default.
DRIVERS=\
	plan9\
	bj10e\
	bjc600\
	bjc800\
	cdj1600\
	cdj670\
	cdj850\
	cdj890\
	dfaxlow\
	epsonc\
	epswrite\
	inferno\
	jpeg\
	jpeggray\
	laserjet\
	ljet2p\
	ljet3\
	ljet3d\
	ljet4\
	pbm\
	pbmraw\
	pdfwrite\
	pgm\
	pgmraw\
	plan9bm\
	pnm\
	pnmraw\
	ppm\
	ppmraw\
	pswrite\
	stcolor\
	tiffg32d\
	tiffg3\
	tiffg4\

HFILES=$objtype.h\
	arch.h\

# enforce some startup conditions
x=`{mkdir obj >[2]/dev/null;}


</sys/src/cmd/mkone

UPDATE=\
	/$objtype/bin/gs\
	/sys/man/1/gs\
	/rc/bin/ps2pdf\
	/rc/bin/pdf2ps\
	/sys/man/1/ps2pdf\

update:V:
	update $UPDATEFLAGS $UPDATE `{cat lsr}


space=" "
CC=pcc
CFLAGS=-c -DDEBUG -DPlan9 -DSTDC '-DWHICH_CMS="lcms2"' -D_POSIX_SOURCE -D_BSD_EXTENSION -DFT2_BUILD_LIBRARY\
	-I. -Ilcms2/include -Ifreetype/include -Ibase -Ijpeg -Itiff/libtiff -Izlib -Iicclib -DT$objtype -I./psi -I./obj -I./obj -I./base '-DGS_INIT="gs_init.ps"'
LD=pcc

CCAUX=pcc
CCAUX_= $CCAUX $CFLAGS

obj/gconfig.$O: base/gconf.c
	$CC $CFLAGS base/gconf.c; mv gconf.$O obj/gconfig.$O

obj/gscdefs.$O: base/gscdef.c
	$CC $CFLAGS base/gscdef.c; mv gscdef.$O obj/gscdefs.$O

obj/iconfig.$O:
	$CC $CFLAGS psi/iconf.c; mv iconf.$O obj/iconfig.$O

obj/%.$O: icclib/%.c
	$CC $CFLAGS icclib/$stem.c; mv $stem.$O obj

obj/%.$O: jpeg/%.c
	$CC $CFLAGS jpeg/$stem.c; mv $stem.$O obj

obj/%.$O: zlib/%.c
	$CC $CFLAGS zlib/$stem.c; mv $stem.$O obj

obj/gdevjpeg.$O:
	./obj/aux/echogs -w ./obj/jconfig0.h -+R ./base/stdpn.h -+R ./base/stdpre.h -+R ./base/gsjconf.h
	cp obj/jconfig0.h obj/jconfig.h
	cp jpeg/jmorecfg.h obj/jmcorig.h
	cp jpeg/jpeglib.h obj/jpeglib_.h
	$CC $CFLAGS base/gdevjpeg.c; mv gdevjpeg.$O obj

obj/%.$O:	base/%.c
	$CC $CFLAGS base/$stem.c; mv $stem.$O obj

obj/%.$O:	psi/%.c
	$CC $CFLAGS psi/$stem.c; mv $stem.$O obj

obj/%.$O:	contrib/%.c
	$CC $CFLAGS contrib/$stem.c; mv $stem.$O obj

obj/%.$O:	lcms2/src/%.c
	$CC $CFLAGS lcms2/src/$stem.c; mv $stem.$O obj

obj/%.$O:	tiff/libtiff/%.c
	$CC $CFLAGS tiff/libtiff/$stem.c; mv $stem.$O obj

obj/%.$O: freetype/src/autofit/%.c
  $CC $CFLAGS freetype/src/autofit/$stem.c; mv $stem.$O obj
obj/%.$O: freetype/src/base/%.c
  $CC $CFLAGS freetype/src/base/$stem.c; mv $stem.$O obj
obj/%.$O: freetype/src/bdf/%.c
  $CC $CFLAGS freetype/src/bdf/$stem.c; mv $stem.$O obj
obj/%.$O: freetype/src/bzip2/%.c
  $CC $CFLAGS freetype/src/bzip2/$stem.c; mv $stem.$O obj
obj/%.$O: freetype/src/cache/%.c
  $CC $CFLAGS freetype/src/cache/$stem.c; mv $stem.$O obj
obj/%.$O: freetype/src/cff/%.c
  $CC $CFLAGS freetype/src/cff/$stem.c; mv $stem.$O obj
obj/%.$O: freetype/src/cid/%.c
  $CC $CFLAGS freetype/src/cid/$stem.c; mv $stem.$O obj
obj/%.$O: freetype/src/gxvalid/%.c
  $CC $CFLAGS freetype/src/gxvalid/$stem.c; mv $stem.$O obj

#obj/adler32.$O: freetype/src/gzip/adler32.c
#  $CC $CFLAGS freetype/src/gzip/adler32.c; mv adler32.$O obj
obj/ftgzip.$O: freetype/src/gzip/ftgzip.c
  $CC $CFLAGS freetype/src/gzip/ftgzip.c; mv ftgzip.$O obj
#obj/infblock.$O: freetype/src/gzip/infblock.c
#  $CC $CFLAGS freetype/src/gzip/infblock.c; mv infblock.$O obj
#obj/infcodes.$O: freetype/src/gzip/infcodes.c
#  $CC $CFLAGS freetype/src/gzip/infcodes.c; mv infcodes.$O obj
#obj/inflate.$O: freetype/src/gzip/inflate.c
#  $CC $CFLAGS freetype/src/gzip/inflate.c; mv inflate.$O obj
#obj/inftrees.$O: freetype/src/gzip/inftrees.c
#  $CC $CFLAGS freetype/src/gzip/inftrees.c; mv inftrees.$O obj
#obj/infutil.$O: freetype/src/gzip/infutil.c
#  $CC $CFLAGS freetype/src/gzip/infutil.c; mv infutil.$O obj
#obj/zutil.$O: freetype/src/gzip/zutil.c
#  $CC $CFLAGS freetype/src/gzip/zutil.c; mv zutil.$O obj

obj/%.$O: freetype/src/lzw/%.c
  $CC $CFLAGS freetype/src/lzw/$stem.c; mv $stem.$O obj
obj/%.$O: freetype/src/otvalid/%.c
  $CC $CFLAGS freetype/src/otvalid/$stem.c; mv $stem.$O obj
obj/%.$O: freetype/src/pcf/%.c
  $CC $CFLAGS freetype/src/pcf/$stem.c; mv $stem.$O obj
obj/%.$O: freetype/src/pfr/%.c
  $CC $CFLAGS freetype/src/pfr/$stem.c; mv $stem.$O obj
obj/%.$O: freetype/src/psaux/%.c
  $CC $CFLAGS freetype/src/psaux/$stem.c; mv $stem.$O obj
obj/%.$O: freetype/src/pshinter/%.c
  $CC $CFLAGS freetype/src/pshinter/$stem.c; mv $stem.$O obj
obj/%.$O: freetype/src/psnames/%.c
  $CC $CFLAGS freetype/src/psnames/$stem.c; mv $stem.$O obj
obj/%.$O: freetype/src/raster/%.c
  $CC $CFLAGS freetype/src/raster/$stem.c; mv $stem.$O obj

obj/sfdriver.$O: freetype/src/sfnt/sfdriver.c
  $CC $CFLAGS freetype/src/sfnt/sfdriver.c; mv sfdriver.$O obj
obj/sfnt.$O: freetype/src/sfnt/sfnt.c
  $CC $CFLAGS freetype/src/sfnt/sfnt.c; mv sfnt.$O obj
obj/sfntpic.$O: freetype/src/sfnt/sfntpic.c
  $CC $CFLAGS freetype/src/sfnt/sfntpic.c; mv sfntpic.$O obj
obj/sfobjs.$O: freetype/src/sfnt/sfobjs.c
  $CC $CFLAGS freetype/src/sfnt/sfobjs.c; mv sfobjs.$O obj
obj/ttbdf.$O: freetype/src/sfnt/ttbdf.c
  $CC $CFLAGS freetype/src/sfnt/ttbdf.c; mv ttbdf.$O obj
obj/ttcmap.$O: freetype/src/sfnt/ttcmap.c
  $CC $CFLAGS freetype/src/sfnt/ttcmap.c; mv ttcmap.$O obj
obj/ttkern.$O: freetype/src/sfnt/ttkern.c
  $CC $CFLAGS freetype/src/sfnt/ttkern.c; mv ttkern.$O obj
obj/ft2ttload.$O: freetype/src/sfnt/ttload.c
  $CC $CFLAGS freetype/src/sfnt/ttload.c; mv ttload.$O obj/ft2ttload.$O
obj/ttmtx.$O: freetype/src/sfnt/ttmtx.c
  $CC $CFLAGS freetype/src/sfnt/ttmtx.c; mv ttmtx.$O obj
obj/ttpost.$O: freetype/src/sfnt/ttpost.c
  $CC $CFLAGS freetype/src/sfnt/ttpost.c; mv ttpost.$O obj
obj/ttsbit.$O: freetype/src/sfnt/ttsbit.c
  $CC $CFLAGS freetype/src/sfnt/ttsbit.c; mv ttsbit.$O obj
obj/ttsbit0.$O: freetype/src/sfnt/ttsbit0.c
  $CC $CFLAGS freetype/src/sfnt/ttsbit0.c; mv ttsbit0.$O obj

obj/%.$O: freetype/src/smooth/%.c
  $CC $CFLAGS freetype/src/smooth/$stem.c; mv $stem.$O obj
obj/%.$O: freetype/src/tools/%.c
  $CC $CFLAGS freetype/src/tools/$stem.c; mv $stem.$O obj

obj/truetype.$O: freetype/src/truetype/truetype.c
  $CC $CFLAGS freetype/src/truetype/truetype.c; mv truetype.$O obj

obj/ttdriver.$O: freetype/src/truetype/ttdriver.c
  $CC $CFLAGS freetype/src/truetype/ttdriver.c; mv ttdriver.$O obj

obj/ttgload.$O: freetype/src/truetype/ttgload.c
  $CC $CFLAGS freetype/src/truetype/ttgload.c; mv ttgload.$O obj

obj/ttgxvar.$O: freetype/src/truetype/ttgxvar.c
  $CC $CFLAGS freetype/src/truetype/ttgxvar.c; mv ttgxvar.$O obj

obj/ft2ttinterp.$O: freetype/src/truetype/ttinterp.c
  $CC $CFLAGS freetype/src/truetype/ttinterp.c; mv ttinterp.$O obj/ft2ttinterp.$O

obj/ft2ttobjs.$O: freetype/src/truetype/ttobjs.c
  $CC $CFLAGS freetype/src/truetype/ttobjs.c; mv ttobjs.$O obj/ft2ttobjs.$O

obj/ttpic.$O: freetype/src/truetype/ttpic.c
  $CC $CFLAGS freetype/src/truetype/ttpic.c; mv ttpic.$O obj

obj/ttpload.$O: freetype/src/truetype/ttpload.c
  $CC $CFLAGS freetype/src/truetype/ttpload.c; mv ttpload.$O obj

obj/%.$O: freetype/src/type1/%.c
  $CC $CFLAGS freetype/src/type1/$stem.c; mv $stem.$O obj
obj/%.$O: freetype/src/type42/%.c
  $CC $CFLAGS freetype/src/type42/$stem.c; mv $stem.$O obj
obj/%.$O: freetype/src/winfonts/%.c
  $CC $CFLAGS freetype/src/winfonts/$stem.c; mv $stem.$O obj

#if possible, use genarch to build $objtype.h; it must run on same cpu type.
#if not, try to use a default header for the target architecture.
$objtype.h:	$O.genarch
	if(~ $objtype $cputype) {
		./$O.genarch $target
		rm -f $O.genarch
	}
	if not {
		if (test -r default.$objtype.h)
			cp default.$objtype.h $target && chmod 664 $target
		if not {
			echo 'no default header file for target architecture'
			echo 'run mk on the target architecture to build one'
			exit false
		}
	}

$O.genarch:U:	base/genarch.c
	if(~ $objtype $cputype) {
		rfork e
		objtype=$cputype
		pcc -DHAVE_LONG_LONG -B -o $O.genarch base/genarch.c
	}
	if not
		status=''

libinstall:V:
	cp lib/* /sys/lib/ghostscript

nuke clean:V:
	rm -f *.[$OS] obj/* [$OS].out y.tab.? y.debug y.output $TARG base/plan9.mak 
