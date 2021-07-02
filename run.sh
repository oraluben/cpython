#set -e
#set -x

M=logging,json,decimal

PROG="import $M"
P=./python  # Linux
if [[ $OSTYPE == 'darwin'* ]]; then
  P=./python.exe
fi

#export PYTHONHASHSEED=0
#export PYTHONPROFILEIMPORTTIME=1

export PYCDSARCHIVE=heap.img
export PYCDSLIST=heap.lst

export PYCDSVERBOSE=1
PYDUMPMODULELIST=heap.lst $P -c "$PROG" >/dev/null 2>&1
PYCDSMODE=DUMP $P >/dev/null 2>&1
PYCDSMODE=SHARE $P -c "$PROG" 2>&1 | grep 'mmap success elapsed'
time PYCDSMODE=SHARE $P -c "$PROG" >/dev/null 2>&1
time $P -c "$PROG" >/dev/null 2>&1

echo
export PYCDSVERBOSE=2
export PYCDSMODE=
$P -c "$PROG" 2>&1| grep get_code | awk '{s+=$4} END {print s}'
$P -c "$PROG" 2>&1| grep find_spec | awk '{s+=$4} END {print s}'
