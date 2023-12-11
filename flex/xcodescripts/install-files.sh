set -e -x

INCDIR="$DSTROOT/$DT_TOOLCHAIN_DIR"/usr/include
install -d -o root -g wheel -m 0755 "$INCDIR"
install -c -o root -g wheel -m 0644 \
	"$PROJECT_DIR"/src/FlexLexer.h \
	"$INCDIR"

BINDIR="$DSTROOT/$DT_TOOLCHAIN_DIR"/usr/bin
install -c -o root -g wheel -m 0755 \
	"$PROJECT_DIR"/lex.sh \
	"$BINDIR"/lex
ln -f "$BINDIR"/flex "$BINDIR"/flex++

MANDIR="$DSTROOT/$DT_TOOLCHAIN_DIR"/usr/share/man/man1
install -d -o root -g wheel -m 0755 "$MANDIR"
install -c -o root -g wheel -m 0644 \
	"$PROJECT_DIR"/doc/flex.1 \
	"$MANDIR"
printf "1d\nw\nq\n" | ed -s "$MANDIR"/flex.1
ln -f "$MANDIR"/flex.1 "$MANDIR"/lex.1
ln -f "$MANDIR"/flex.1 "$MANDIR"/flex++.1

OSV="$DSTROOT"/usr/local/OpenSourceVersions
OSL="$DSTROOT"/usr/local/OpenSourceLicenses
install -d -o root -g wheel -m 0755 "$OSV"
install -c -o root -g wheel -m 0644 \
	"$PROJECT_DIR"/flex.plist \
	"$OSV"
install -d -o root -g wheel -m 0755 "$OSL"
install -c -o root -g wheel -m 0644 \
	"$PROJECT_DIR"/COPYING \
	"$OSL"/flex.txt
