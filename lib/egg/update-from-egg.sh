#!/bin/sh

function die() {
  echo $*
  exit 1
}

if test -z "$EGGDIR"; then
   echo "Must set EGGDIR"
   exit 1
fi

if test -z "$EGGFILES"; then
   echo "Must set EGGFILES"
   exit 1
fi

for FILE in $EGGFILES; do
  SRCFILE=$EGGDIR/$FILE
  if ! test -e $SRCFILE ; then
      if test -e $EGGDIR/toolbar/$FILE ; then
          SRCFILE=$EGGDIR/toolbar/$FILE
      fi
      if test -e $EGGDIR/menu/$FILE ; then
          SRCFILE=$EGGDIR/menu/$FILE
      fi
      if test -e $EGGDIR/toolbareditor/$FILE ; then
          SRCFILE=$EGGDIR/toolbareditor/$FILE
      fi
      if test -e $EGGDIR/util/$FILE ; then
          SRCFILE=$EGGDIR/util/$FILE
      fi
      if test -e $EGGDIR/treeviewutils/$FILE ; then
          SRCFILE=$EGGDIR/treeviewutils/$FILE
      fi
  fi
  if cmp -s $SRCFILE $FILE; then
     echo "File $FILE is unchanged"
  else
     cp $SRCFILE $FILE || die "Could not move $SRCFILE to $FILE"
     echo "Updated $FILE"
  fi
done
