#!/bin/sh

#--------------------------------------------------------------------------#

die () {
  cecho "${HIDE}test/run.sh:${NORMAL} ${BAD}error:${NORMAL} $*"
  exit 1
}

msg () {
  cecho "${HIDE}test/run.sh:${NORMAL} $*"
}

for dir in . .. ../..
do
  [ -f $dir/colors.sh ] || continue
  . $dir/colors.sh || exit 1
  break
done

#--------------------------------------------------------------------------#

[ -d ../test -a -d ../test ] || \
die "needs to be called from a top-level sub-directory of CaDiCaL"

[ x"$CADICALBUILD" = x ] && CADICALBUILD="../build"

[ -x "$CADICALBUILD/cadicraig" ] || \
  die "can not find '$CADICALBUILD/cadicraig' (run 'make' first)"

cecho -n "$HILITE"
cecho "---------------------------------------------------------"
cecho "CCNF testing in '$CADICALBUILD'" 
cecho "---------------------------------------------------------"
cecho -n "$NORMAL"

make -C $CADICALBUILD
res=$?
[ $res = 0 ] || exit $res

#--------------------------------------------------------------------------#

solver="$CADICALBUILD/cadicraig"

#--------------------------------------------------------------------------#

ok=0
failed=0

run () {
  msg "running CCNF tests ${HILITE}'$1'${NORMAL}"
  prefix=$CADICALBUILD/test-ccnf
  log=$prefix-$1.log
  err=$prefix-$1.err
  out=$prefix-$1.out
  cnf=../test/$1.ccnf
  sol=../test/$1.sol
  opts="-w $out $cnf"
  cecho "  execute $solver $opts"
  cecho -n "  # $2 ..."
  "$solver" $opts 1>$log 2>$err
  res=$?
  if [ ! $res = $2 ] 
  then
    cecho " ${BAD}FAILED${NORMAL} (actual exit code $res)"
    failed=`expr $failed + 1`
  else
    diff -w $sol $out 1>/dev/null 2>/dev/null
    res=$?
    if [ ! $res = 0 ]
    then
      cecho " ${BAD}FAILED${NORMAL} (result different)"
      cat $out
      cecho "  ${BAD}EXPECTED${NORMAL}"
      cat $sol
      cecho ""
      failed=`expr $failed + 1`
    else
      cecho " ${GOOD}ok${NORMAL} (exit code '$res' as expected)"
      ok=`expr $ok + 1`
    fi
  fi
}

run const0 20
run const1 20
run negative 20
run positive 20
run basic1 20
run confl1 20
run confl2 20

#--------------------------------------------------------------------------#

[ $ok -gt 0 ] && OK="$GOOD"
[ $failed -gt 0 ] && FAILED="$BAD"

msg "${HILITE}CCNF testing results:${NORMAL} ${OK}$ok ok${NORMAL}, ${FAILED}$failed failed${NORMAL}"

exit $failed
